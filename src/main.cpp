#include "globals.hpp"
#include "hyprtrizoneLayout.hpp"
#include "pluginConfig.hpp"
#include "StackIndicatorPassElement.hpp"
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
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

        void reset() {
            disarmSignalListener(render);
            disarmSignalListener(workspaceActive);
            disarmSignalListener(windowDestroy);

            render.reset();
            workspaceActive.reset();
            windowDestroy.reset();
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

    s_pluginListeners.render = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (GPLUGIN_SHUTTING_DOWN.load())
            return;

        if (stage != RENDER_POST_WINDOWS)
            return;

        if (CHyprtrizoneAlgorithm::instances().empty())
            return;

        auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
        if (!monitor || !monitor->m_activeWorkspace)
            return;

        if (CHyprtrizoneAlgorithm::activeOnMonitor(monitor)) {
            g_pHyprRenderer->m_renderPass.add(makeUnique<CStackIndicatorPassElement>());
        }
    });

    s_pluginListeners.workspaceActive = Event::bus()->m_events.workspace.active.listen([](PHLWORKSPACE pWorkspace) {
        if (GPLUGIN_SHUTTING_DOWN.load())
            return;

        if (!pWorkspace || s_followWorkspaceMoveGuard)
            return;

        if (pWorkspace->m_isSpecialWorkspace || pWorkspace->m_id <= 0)
            return;

        const auto pMonitor = pWorkspace->m_monitor.lock();
        if (!pMonitor)
            return;

        std::vector<PHLWINDOW> candidates;
        candidates.reserve(Desktop::windowState()->windows().size());

        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w || !w->m_isMapped || w->isHidden() || w->m_isFloating || w->m_pinned)
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
            if (!w || !w->m_isMapped || w->isHidden() || w->m_isFloating || w->m_pinned || w->m_workspace == pWorkspace)
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

    return {"hyprtrizone", "Three-zone layout with scrolling side stacks and window pairing", "nyrda", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    GPLUGIN_SHUTTING_DOWN.store(true);
    s_pluginListeners.reset();

    // The renderer can still be holding plugin-owned pass elements from the
    // previous frame. Clear them while this .so is still loaded so their
    // destructors do not run after unload.
    if (g_pHyprRenderer)
        g_pHyprRenderer->m_renderPass.clear();

    HyprlandAPI::removeAlgo(PHANDLE, "hyprtrizone");
}
