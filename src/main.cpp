#include "globals.hpp"
#include "hyprtrizoneLayout.hpp"
#include "pluginConfig.hpp"
#include "StackIndicatorPassElement.hpp"
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/signal/Listener.hpp>
#include <functional>
#include <vector>

static bool s_followWorkspaceMoveGuard = false;

namespace {
    struct SSignalListenerLayout {
        std::function<void(void*)> handler;
    };

    static_assert(sizeof(SSignalListenerLayout) == sizeof(Hyprutils::Signal::CSignalListener));

    void disarmSignalListener(const CHyprSignalListener& listener) {
        if (!listener)
            return;

        // Hyprutils signal emissions keep temporary shared references to
        // listeners. If one survives until after dlclose, destroying the
        // plugin-owned std::function target can jump into unmapped code.
        // Replace it with an empty function while the .so is still loaded.
        auto* layout = reinterpret_cast<SSignalListenerLayout*>(listener.get());
        layout->handler = {};
    }

    struct SPluginListeners {
        CHyprSignalListener render;
        CHyprSignalListener workspaceActive;
        CHyprSignalListener windowDestroy;
        CHyprSignalListener mouseAxis;

        void reset() {
            disarmSignalListener(render);
            disarmSignalListener(workspaceActive);
            disarmSignalListener(windowDestroy);
            disarmSignalListener(mouseAxis);

            render.reset();
            workspaceActive.reset();
            windowDestroy.reset();
            mouseAxis.reset();
        }
    };

    static SPluginListeners s_pluginListeners;
}

struct SFollowWorkspaceMoveGuardReset {
    ~SFollowWorkspaceMoveGuardReset() {
        s_followWorkspaceMoveGuard = false;
    }
};

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Hyprland only compares the API version. A build left over from before a Hyprland update would load into
    // the new compositor, so refuse unless the headers came from this exact build. Throwing here unloads the plugin.
    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprtrizone] Built for a different Hyprland. Rebuild the plugin.", CHyprColor{1.0, 0.2, 0.2, 1.0}, 10000);
        throw std::runtime_error("[hyprtrizone] Hyprland version mismatch");
    }

    GPLUGIN_SHUTTING_DOWN.store(false);

    HyprtrizoneConfig::init(PHANDLE);

    HyprlandAPI::addTiledAlgo(
        PHANDLE, "hyprtrizone", &typeid(CHyprtrizoneAlgorithm),
        []() -> UP<Layout::ITiledAlgorithm> { return makeUnique<CHyprtrizoneAlgorithm>(); });

    s_pluginListeners.render = Event::bus()->m_events.render.stage.listen([](Event::SRenderStageEvent event) {
        if (GPLUGIN_SHUTTING_DOWN.load())
            return;

        if (event.stage != RENDER_POST_WINDOWS || !event.context)
            return;

        if (CHyprtrizoneAlgorithm::instances().empty())
            return;

        const auto& monitor = event.monitor;
        if (!monitor || !monitor->m_activeWorkspace)
            return;

        if (CHyprtrizoneAlgorithm::activeOnMonitor(monitor)) {
            g_pHyprRenderer->addPassElement(event.context->get(), makeUnique<CStackIndicatorPassElement>());
        }
    });

    s_pluginListeners.workspaceActive = Event::bus()->m_events.workspace.active.listen([](PHLWORKSPACE pWorkspace) {
        if (GPLUGIN_SHUTTING_DOWN.load())
            return;

        if (!pWorkspace || s_followWorkspaceMoveGuard)
            return;

        // Numbered workspaces only, as before: special and named workspaces have no number.
        if (pWorkspace->type() == Workspace::eWorkspaceType::SPECIAL || !pWorkspace->numberedID())
            return;

        const auto pMonitor = pWorkspace->m_monitor.lock();
        if (!pMonitor)
            return;

        std::vector<PHLWINDOW> candidates;
        candidates.reserve(Desktop::windowState()->windows().size());

        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w || !w->mapped() || w->isHidden() || w->isFloating() || (w->m_state & Desktop::View::WINDOW_STATE_PINNED))
                continue;

            const auto windowMonitor = w->m_monitor.lock();
            if (!windowMonitor || windowMonitor != pMonitor)
                continue;

            if (w->m_workspace == pWorkspace)
                continue;

            auto* algo = CHyprtrizoneAlgorithm::fromWindow(w);
            if (!algo || !algo->shouldWindowFollowWorkspaces(w))
                continue;

            candidates.push_back(w);
        }

        if (candidates.empty())
            return;

        s_followWorkspaceMoveGuard = true;
        const SFollowWorkspaceMoveGuardReset guard;

        for (const auto& w : candidates) {
            if (!w || !w->mapped() || w->isHidden() || w->isFloating() || (w->m_state & Desktop::View::WINDOW_STATE_PINNED) || w->m_workspace == pWorkspace)
                continue;

            auto* algo = CHyprtrizoneAlgorithm::fromWindow(w);
            if (!algo || !algo->shouldWindowFollowWorkspaces(w) || !algo->prepareWindowForWorkspaceFollow(w))
                continue;

            Desktop::globalWindowController()->moveWindowToWorkspace(w, pWorkspace);
        }
    });

    s_pluginListeners.windowDestroy = Event::bus()->m_events.window.destroy.listen([](PHLWINDOWREF) {
        if (GPLUGIN_SHUTTING_DOWN.load())
            return;

        // Emitted from ~CWindow, so the ref can no longer be locked; drop state of windows that are gone.
        CHyprtrizoneAlgorithm::pruneCachedWindowState();
    });

    // Runs before Hyprland's binds, which drop wheel ticks within binds:scroll_event_delay and pass them to the client.
    s_pluginListeners.mouseAxis = Event::bus()->m_events.input.mouse.axis.listen([](IPointer::SAxisEvent event, Event::SCallbackInfo& info) {
        if (GPLUGIN_SHUTTING_DOWN.load() || info.cancelled)
            return;

        if (CHyprtrizoneAlgorithm::handleWheelEvent(event))
            info.cancelled = true;
    });

    return {"hyprtrizone", "Three-zone layout with scrolling side stacks and window pairing", "nyrda", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    GPLUGIN_SHUTTING_DOWN.store(true);
    s_pluginListeners.reset();

    // The renderer can still be holding plugin-owned pass elements from the
    // previous frame. Clear them while this .so is still loaded so their
    // destructors do not run after unload.
    if (g_pHyprRenderer)
        g_pHyprRenderer->context().m_pass.clear();

    HyprlandAPI::removeAlgo(PHANDLE, "hyprtrizone");
}
