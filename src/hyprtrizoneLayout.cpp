#include "hyprtrizoneLayout.hpp"
#include "pluginConfig.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/supplementary/DragController.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/fullscreen/handler/FullscreenHandler.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/keybinds/Manager.hpp>
#include <hyprland/src/keybinds/Resolver.hpp>
#include <hyprland/src/pointer/PointerController.hpp>
#include <hyprland/src/protocols/InputCapture.hpp>
#include <hyprland/src/protocols/ShortcutsInhibit.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/CHyprGroupBarDecoration.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprutils/string/VarList2.hpp>
#include "StackIndicatorPassElement.hpp"

using Hyprutils::String::CVarList2;

// ─── Zone cache (survives algorithm destruction/creation) ───────────────────

struct SCachedNodeInfo {
    eHyprtrizoneZone zone;
    bool                   pairWithPrevious;
    int                    orderInZone; // 0-based position within same-zone nodes
    std::string            workspaceAddress; // the workspace's addressableName(); empty for none
    bool                   pairedWithNext = false; // only recorded for the removed target itself
    uint64_t               restoreBatch   = 0;     // layout-switch snapshot; orderInZone is then relative to the whole snapshot
};

static uint64_t s_lastRestoreBatch = 0;

struct SFollowWorkspaceMoveInfo {
    eHyprtrizoneZone zone;
    std::string            sourceWorkspaceAddress;
};

static std::unordered_map<uint64_t, SCachedNodeInfo> s_zoneCacheByStableID;
static std::unordered_map<uint64_t, SFollowWorkspaceMoveInfo> s_followWorkspaceMovesByStableID;

static std::string trimTag(std::string tag) {
    const auto first = tag.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";

    const auto last = tag.find_last_not_of(" \t\n\r");
    return tag.substr(first, last - first + 1);
}

static bool windowHasAnyConfiguredTag(PHLWINDOW pWindow, const std::string& configured) {
    if (!pWindow || configured.empty())
        return false;

    CVarList2 tagList(std::string(configured), 0, ',');
    for (auto& t : tagList) {
        const auto tag = trimTag(std::string(t));
        if (tag.empty())
            continue;

        if (pWindow->m_ruleApplicator->m_tagKeeper.isTagged(tag))
            return true;
    }

    return false;
}

static int getPluginInt(const char* key, int fallback = 0) {
    return HyprtrizoneConfig::getInt(key, fallback);
}

static float getPluginFloat(const char* key, float fallback = 0.F) {
    return HyprtrizoneConfig::getFloat(key, fallback);
}

static std::string getPluginString(const char* key, std::string fallback = {}) {
    return HyprtrizoneConfig::getString(key, std::move(fallback));
}

// ─── Global state ──────────────────────────────────────────────────────────

std::optional<bool> CHyprtrizoneGlobals::scrollingEnabledOverride;
std::optional<bool> CHyprtrizoneGlobals::pairingLeftOverride;
std::optional<bool> CHyprtrizoneGlobals::pairingRightOverride;

bool CHyprtrizoneGlobals::isScrollingEnabled() {
    if (scrollingEnabledOverride.has_value())
        return *scrollingEnabledOverride;

    return getPluginInt("plugin:hyprtrizone:scrolling_enabled", 1) != 0;
}

// ─── Static instance registry ──────────────────────────────────────────────

std::set<CHyprtrizoneAlgorithm*>& CHyprtrizoneAlgorithm::instances() {
    static std::set<CHyprtrizoneAlgorithm*> s;
    return s;
}

CHyprtrizoneAlgorithm* CHyprtrizoneAlgorithm::fromWindow(PHLWINDOW pWindow) {
    if (!pWindow)
        return nullptr;

    const auto target = pWindow->layoutTarget();
    if (!target)
        return nullptr;

    const auto space = target->space();
    if (!space)
        return nullptr;

    const auto algorithm = space->algorithm();
    if (!algorithm)
        return nullptr;

    const auto& tiledAlgo = algorithm->tiledAlgo();
    if (!tiledAlgo)
        return nullptr;

    return dynamic_cast<CHyprtrizoneAlgorithm*>(tiledAlgo.get());
}

void CHyprtrizoneAlgorithm::pruneCachedWindowState() {
    std::unordered_set<uint64_t> liveStableIDs;
    for (const auto& w : Desktop::windowState()->windows()) {
        if (w)
            liveStableIDs.insert(w->metadata().stableID());
    }

    std::erase_if(s_zoneCacheByStableID, [&](const auto& entry) { return !liveStableIDs.contains(entry.first); });
    std::erase_if(s_followWorkspaceMovesByStableID, [&](const auto& entry) { return !liveStableIDs.contains(entry.first); });
}

// ─── Constructor / Destructor ──────────────────────────────────────────────

CHyprtrizoneAlgorithm::CHyprtrizoneAlgorithm() {
    m_masterFactor = getPluginFloat("plugin:hyprtrizone:master_factor", 0.5F);

    instances().insert(this);
}

CHyprtrizoneAlgorithm::~CHyprtrizoneAlgorithm() {
    instances().erase(this);
}

// ─── Workspace / Monitor / WorkArea helpers ────────────────────────────────

PHLWORKSPACE CHyprtrizoneAlgorithm::workspace() const {
    auto parent = m_parent.lock();
    if (!parent)
        return nullptr;
    auto space = parent->space();
    if (!space)
        return nullptr;
    return space->workspace();
}

PHLMONITOR CHyprtrizoneAlgorithm::getMonitor() const {
    auto ws = workspace();
    return ws ? ws->m_monitor.lock() : nullptr;
}

CBox CHyprtrizoneAlgorithm::getWorkArea() const {
    auto parent = m_parent.lock();
    if (!parent)
        return {};
    auto space = parent->space();
    if (!space)
        return {};
    return space->workArea();
}

CHyprtrizoneAlgorithm* CHyprtrizoneAlgorithm::activeOnMonitor(PHLMONITOR pMonitor) {
    if (!pMonitor)
        return nullptr;

    for (auto* algo : instances()) {
        const auto ws = algo ? algo->workspace() : nullptr;
        if (!ws)
            continue;

        if (ws == pMonitor->m_activeWorkspace || ws == pMonitor->m_activeSpecialWorkspace)
            return algo;
    }

    return nullptr;
}

bool CHyprtrizoneAlgorithm::handleWheelEvent(const IPointer::SAxisEvent& event) {
    if (event.source != WL_POINTER_AXIS_SOURCE_WHEEL || event.axis != WL_POINTER_AXIS_VERTICAL_SCROLL || event.delta == 0.0)
        return false;

    const std::string modsConfig = getPluginString("plugin:hyprtrizone:wheel_scroll_mods");
    if (modsConfig.empty())
        return false;

    const auto mods = Keybinds::modMaskFromString(modsConfig);
    if (!mods || g_pInputManager->getModsFromAllKBs() != mods)
        return false;

    // Leave the event alone where Hyprland would not run a bind either.
    static CConfigValue<Config::INTEGER> disableKeybindGrabbing{"binds:disable_keybind_grabbing"};
    if (g_pSessionLockManager->isSessionLocked() || PROTO::inputCapture->isCaptured() || !Keybinds::mgr()->currentSubmap().empty() ||
        (!*disableKeybindGrabbing && PROTO::shortcutsInhibit->isInhibited()))
        return false;

    if (!CHyprtrizoneGlobals::isScrollingEnabled())
        return false;

    const auto cursor  = g_pInputManager->getMouseCoordsInternal();
    const auto monitor = State::monitorState()->query().vec(cursor).run();
    if (!monitor)
        return false;

    // An open special workspace covers the regular one.
    const auto workspace = monitor->m_activeSpecialWorkspace ? monitor->m_activeSpecialWorkspace : monitor->m_activeWorkspace;
    if (!workspace)
        return false;

    const auto algoIt = std::ranges::find_if(instances(), [&](auto* algo) { return algo && algo->workspace() == workspace; });
    if (algoIt == instances().end())
        return false;

    auto* algo = *algoIt;
    auto  zone = algo->getZoneAtCursor();
    if (zone == HYPRTRIZONE_ZONE_CENTER)
        zone = algo->getActiveZone();
    if (zone == HYPRTRIZONE_ZONE_CENTER)
        zone = algo->getBiasZone();

    // A high-resolution wheel reports fractions of a 120-unit notch; a notch is 15 units of delta.
    const float notches = event.deltaDiscrete != 0 ? std::abs(event.deltaDiscrete) / 120.f : (float)std::abs(event.delta) / 15.f;
    algo->wheelScrollZone(zone, event.delta > 0.0 ? 1.f : -1.f, notches);
    return true;
}

int CHyprtrizoneAlgorithm::getZoneHeaderHeight(eHyprtrizoneZone zone) const {
    if (zone == HYPRTRIZONE_ZONE_LEFT)
        return std::max(0, getPluginInt("plugin:hyprtrizone:left_header_height", 0));
    if (zone == HYPRTRIZONE_ZONE_RIGHT)
        return std::max(0, getPluginInt("plugin:hyprtrizone:right_header_height", 0));

    return 0;
}

CBox CHyprtrizoneAlgorithm::getZoneBox(eHyprtrizoneZone zone) const {
    const auto WORKAREA = getWorkArea();
    if (WORKAREA.w <= 0 || WORKAREA.h <= 0)
        return {};

    const float masterWidth = WORKAREA.w * m_masterFactor;
    const float sideWidth   = (WORKAREA.w - masterWidth) / 2.0f;

    switch (zone) {
        case HYPRTRIZONE_ZONE_LEFT: return {WORKAREA.x, WORKAREA.y, sideWidth, WORKAREA.h};
        case HYPRTRIZONE_ZONE_CENTER: return {WORKAREA.x + sideWidth, WORKAREA.y, masterWidth, WORKAREA.h};
        case HYPRTRIZONE_ZONE_RIGHT: return {WORKAREA.x + sideWidth + masterWidth, WORKAREA.y, sideWidth, WORKAREA.h};
    }

    return {};
}

CBox CHyprtrizoneAlgorithm::getZoneContentBox(eHyprtrizoneZone zone) const {
    const auto zoneBox = getZoneBox(zone);
    if (zone == HYPRTRIZONE_ZONE_CENTER)
        return zoneBox;

    const double headerHeight = std::min((double)getZoneHeaderHeight(zone), zoneBox.h);
    return {zoneBox.x, zoneBox.y + headerHeight, zoneBox.w, std::max(0.0, zoneBox.h - headerHeight)};
}

// ─── Node helpers ──────────────────────────────────────────────────────────

CHyprtrizoneAlgorithm::SNodeData* CHyprtrizoneAlgorithm::getNodeFromTarget(SP<Layout::ITarget> target) {
    for (auto& nd : m_nodes) {
        if (nd.target.lock() == target)
            return &nd;
    }
    return nullptr;
}

CHyprtrizoneAlgorithm::SNodeData* CHyprtrizoneAlgorithm::getNodeFromWindow(PHLWINDOW pWindow) {
    for (auto& nd : m_nodes) {
        auto t = nd.target.lock();
        if (t && t->window() == pWindow)
            return &nd;
    }
    return nullptr;
}

CHyprtrizoneAlgorithm::SNodeData* CHyprtrizoneAlgorithm::getMasterNode() {
    for (auto& nd : m_nodes) {
        if (nd.zone == HYPRTRIZONE_ZONE_CENTER)
            return &nd;
    }
    return nullptr;
}

int CHyprtrizoneAlgorithm::getNodesInZone(eHyprtrizoneZone zone) const {
    int count = 0;
    for (const auto& nd : m_nodes) {
        if (nd.zone == zone)
            count++;
    }
    return count;
}

int CHyprtrizoneAlgorithm::getNodeCount() const {
    return m_nodes.size();
}

SP<Layout::ITarget> CHyprtrizoneAlgorithm::getFocusedTarget() {
    auto window = Desktop::focusState()->window();
    if (!window)
        return nullptr;
    for (auto& nd : m_nodes) {
        auto t = nd.target.lock();
        if (t && t->window() == window)
            return t;
    }
    return nullptr;
}

// ─── Zone helpers ──────────────────────────────────────────────────────────

eHyprtrizoneZone CHyprtrizoneAlgorithm::getActiveZone() const {
    auto window = Desktop::focusState()->window();
    if (window) {
        for (const auto& nd : m_nodes) {
            auto t = nd.target.lock();
            if (t && t->window() == window)
                return nd.zone;
        }
    }
    return HYPRTRIZONE_ZONE_CENTER;
}

eHyprtrizoneZone CHyprtrizoneAlgorithm::getZoneAtCursor() const {
    const auto WORKAREA = getWorkArea();
    if (WORKAREA.w <= 0)
        return HYPRTRIZONE_ZONE_CENTER;

    float masterWidth = WORKAREA.w * m_masterFactor;
    float sideWidth   = (WORKAREA.w - masterWidth) / 2.0f;
    float leftEnd     = WORKAREA.x + sideWidth;
    float rightStart  = WORKAREA.x + sideWidth + masterWidth;

    const auto CURSOR = g_pInputManager->getMouseCoordsInternal();

    if (CURSOR.x < leftEnd)
        return HYPRTRIZONE_ZONE_LEFT;
    else if (CURSOR.x >= rightStart)
        return HYPRTRIZONE_ZONE_RIGHT;
    else
        return HYPRTRIZONE_ZONE_CENTER;
}

eHyprtrizoneZone CHyprtrizoneAlgorithm::getBiasZone() const {
    const std::string bias = getPluginString("plugin:hyprtrizone:side_bias", "right");
    if (bias == "left" || bias == "l" || bias == "0")
        return HYPRTRIZONE_ZONE_LEFT;

    return HYPRTRIZONE_ZONE_RIGHT;
}

// ─── Scrolling state helpers ───────────────────────────────────────────────

float CHyprtrizoneAlgorithm::getSideScrollOffset(eHyprtrizoneZone zone) const {
    if (zone == HYPRTRIZONE_ZONE_LEFT)
        return m_leftScrollOffset;
    if (zone == HYPRTRIZONE_ZONE_RIGHT)
        return m_rightScrollOffset;
    return 0.f;
}

void CHyprtrizoneAlgorithm::setSideScrollOffset(eHyprtrizoneZone zone, float offset) {
    if (zone == HYPRTRIZONE_ZONE_LEFT)
        m_leftScrollOffset = offset;
    else if (zone == HYPRTRIZONE_ZONE_RIGHT)
        m_rightScrollOffset = offset;
}

// ─── Scrolling math helpers ────────────────────────────────────────────────

int CHyprtrizoneAlgorithm::getSideVisibleWindowCount(eHyprtrizoneZone zone) const {
    const int fallbackVisible = std::max(1, getPluginInt("plugin:hyprtrizone:side_visible_windows", 3));
    const int leftVisible     = getPluginInt("plugin:hyprtrizone:left_side_visible_windows", 0);
    const int rightVisible    = getPluginInt("plugin:hyprtrizone:right_side_visible_windows", 0);

    if (zone == HYPRTRIZONE_ZONE_LEFT && leftVisible > 0)
        return std::max(1, leftVisible);

    if (zone == HYPRTRIZONE_ZONE_RIGHT && rightVisible > 0)
        return std::max(1, rightVisible);

    return fallbackVisible;
}

float CHyprtrizoneAlgorithm::getSideSlotHeight(eHyprtrizoneZone zone, float workspaceHeight, int sideCount) const {
    if (sideCount <= 0 || workspaceHeight <= 0.f)
        return workspaceHeight;

    if (!CHyprtrizoneGlobals::isScrollingEnabled())
        return workspaceHeight / sideCount;

    const int visible = getSideVisibleWindowCount(zone);
    const int divisor = sideCount >= visible ? visible : sideCount;
    return workspaceHeight / std::max(1, divisor);
}

float CHyprtrizoneAlgorithm::getSideMaxScroll(eHyprtrizoneZone zone, float workspaceHeight, int sideCount) const {
    if (sideCount <= 0 || workspaceHeight <= 0.f)
        return 0.f;

    if (!CHyprtrizoneGlobals::isScrollingEnabled())
        return 0.f;

    const float slotHeight    = getSideSlotHeight(zone, workspaceHeight, sideCount);
    const float contentHeight = slotHeight * sideCount;
    return std::max(0.f, contentHeight - workspaceHeight);
}

void CHyprtrizoneAlgorithm::clampScrollOffsets() {
    const auto leftContent  = getZoneContentBox(HYPRTRIZONE_ZONE_LEFT);
    const auto rightContent = getZoneContentBox(HYPRTRIZONE_ZONE_RIGHT);
    if (leftContent.h <= 0 && rightContent.h <= 0)
        return;

    const auto leftRows     = buildSideRows(HYPRTRIZONE_ZONE_LEFT);
    const auto rightRows    = buildSideRows(HYPRTRIZONE_ZONE_RIGHT);
    const auto leftMetrics  = getSideLayoutMetrics(HYPRTRIZONE_ZONE_LEFT, leftRows, leftContent.h);
    const auto rightMetrics = getSideLayoutMetrics(HYPRTRIZONE_ZONE_RIGHT, rightRows, rightContent.h);

    m_leftScrollOffset  = std::clamp(m_leftScrollOffset, 0.f, std::max(0.f, leftMetrics.contentHeight - (float)leftContent.h));
    m_rightScrollOffset = std::clamp(m_rightScrollOffset, 0.f, std::max(0.f, rightMetrics.contentHeight - (float)rightContent.h));
}

void CHyprtrizoneAlgorithm::snapZoneToEnd(eHyprtrizoneZone zone) {
    if (zone != HYPRTRIZONE_ZONE_LEFT && zone != HYPRTRIZONE_ZONE_RIGHT)
        return;

    const auto contentBox = getZoneContentBox(zone);
    if (contentBox.h <= 0)
        return;

    const auto rows    = buildSideRows(zone);
    const auto metrics = getSideLayoutMetrics(zone, rows, contentBox.h);
    setSideScrollOffset(zone, std::max(0.f, metrics.contentHeight - (float)contentBox.h));
}

// Moves a side stack by a signed number of rows, at least one pixel, times amountMult.
// Returns false when the offset stopped at either end.
bool CHyprtrizoneAlgorithm::scrollZoneByRows(eHyprtrizoneZone zone, float rows, float amountMult) {
    if (zone != HYPRTRIZONE_ZONE_LEFT && zone != HYPRTRIZONE_ZONE_RIGHT)
        return false;

    if (!CHyprtrizoneGlobals::isScrollingEnabled())
        return false;

    const auto contentBox = getZoneContentBox(zone);
    if (contentBox.h <= 0)
        return false;

    const auto sideRows = buildSideRows(zone);
    if (sideRows.empty())
        return false;

    const auto  metrics   = getSideLayoutMetrics(zone, sideRows, contentBox.h);
    const float maxScroll = std::max(0.f, metrics.contentHeight - (float)contentBox.h);

    const float distance = std::max(1.f, metrics.slotHeight * std::abs(rows)) * amountMult;
    const float wanted   = getSideScrollOffset(zone) + (rows < 0.f ? -distance : distance);
    const float offset   = std::clamp(wanted, 0.f, maxScroll);
    setSideScrollOffset(zone, offset);

    recalculate();
    return offset == wanted;
}

void CHyprtrizoneAlgorithm::scrollZone(eHyprtrizoneZone zone, float direction, float amountMult) {
    scrollZoneByRows(zone, direction * std::max(0.05f, getPluginFloat("plugin:hyprtrizone:scroll_step", 0.35F)), std::max(0.01f, amountMult));
}

bool CHyprtrizoneAlgorithm::isZoneMoving(eHyprtrizoneZone zone) const {
    for (const auto& nd : m_nodes) {
        if (nd.zone != zone)
            continue;

        const auto target = nd.target.lock();
        const auto window = target ? target->window() : nullptr;
        if (window && window->positionAnimation()->isBeingAnimated())
            return true;
    }

    return false;
}

void CHyprtrizoneAlgorithm::wheelScrollZone(eHyprtrizoneZone zone, float direction, float notches) {
    // Ticks of a fast spin arrive while the stack still moves towards the previous target. Each of them
    // extends the target from there, and the step grows with the number of such ticks in a row.
    constexpr float MAX_ACCELERATION = 4.f;
    constexpr auto  STREAK_TIMEOUT   = std::chrono::milliseconds(1000);

    auto&      streak = zone == HYPRTRIZONE_ZONE_LEFT ? m_leftWheelStreak : m_rightWheelStreak;
    const auto now    = std::chrono::steady_clock::now();
    // Any other scroll since the previous notch (command, focus, new window) also ends the streak.
    if (streak.direction != direction || now - streak.lastTick > STREAK_TIMEOUT || getSideScrollOffset(zone) != streak.offset || !isZoneMoving(zone))
        streak.notches = 0.f;

    const float acceleration = std::max(0.f, getPluginFloat("plugin:hyprtrizone:wheel_scroll_acceleration", 0.5F));
    const float multiplier   = std::min(MAX_ACCELERATION, 1.f + acceleration * streak.notches);
    const float step         = std::max(0.05f, getPluginFloat("plugin:hyprtrizone:wheel_scroll_step", 0.5F));

    const bool  unclamped = scrollZoneByRows(zone, direction * step, notches * multiplier);

    streak.direction = direction;
    streak.offset    = getSideScrollOffset(zone);
    streak.notches   = unclamped ? streak.notches + notches : 0.f;
    streak.lastTick  = now;
}

void CHyprtrizoneAlgorithm::focusWindowInSideStack(SP<Layout::ITarget> target) {
    if (!target)
        return;

    if (!CHyprtrizoneGlobals::isScrollingEnabled())
        return;

    auto pNode = getNodeFromTarget(target);
    if (!pNode)
        return;

    if (pNode->zone != HYPRTRIZONE_ZONE_LEFT && pNode->zone != HYPRTRIZONE_ZONE_RIGHT)
        return;

    const auto contentBox = getZoneContentBox(pNode->zone);
    if (contentBox.h <= 0)
        return;

    const auto rows      = buildSideRows(pNode->zone);
    const int  sideCount = rows.size();
    if (sideCount <= 0)
        return;

    auto pWindow = target->window();
    int  currentIdx = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        auto firstTarget  = rows[i].first ? rows[i].first->target.lock() : nullptr;
        auto secondTarget = rows[i].second ? rows[i].second->target.lock() : nullptr;
        auto firstWin     = firstTarget ? firstTarget->window() : nullptr;
        auto secondWin    = secondTarget ? secondTarget->window() : nullptr;
        if (firstWin == pWindow || secondWin == pWindow) {
            currentIdx = i;
            break;
        }
    }

    if (currentIdx < 0)
        return;

    const auto  metrics    = getSideLayoutMetrics(pNode->zone, rows, contentBox.h);
    const float maxScroll  = std::max(0.f, metrics.contentHeight - (float)contentBox.h);
    if (metrics.rowHeights.size() != rows.size() || currentIdx >= (int)metrics.rowHeights.size())
        return;

    float currentOffset = getSideScrollOffset(pNode->zone);
    float windowTop     = metrics.rowTops[currentIdx] - currentOffset;
    float windowBottom  = windowTop + metrics.rowHeights[currentIdx];

    float targetOffset = currentOffset;
    if (windowTop < 0.f)
        targetOffset = metrics.rowTops[currentIdx];
    else if (windowBottom > contentBox.h)
        targetOffset = metrics.rowTops[currentIdx] + metrics.rowHeights[currentIdx] - contentBox.h;

    targetOffset = std::clamp(targetOffset, 0.f, maxScroll);
    setSideScrollOffset(pNode->zone, targetOffset);
    recalculate();
}

// ─── Zone preference helpers ──────────────────────────────────────────────

bool CHyprtrizoneAlgorithm::shouldWindowFollowWorkspaces(PHLWINDOW pWindow) const {
    const std::string configured = getPluginString("plugin:hyprtrizone:follow_workspace_tags");
    return windowHasAnyConfiguredTag(pWindow, configured);
}

bool CHyprtrizoneAlgorithm::prepareWindowForWorkspaceFollow(PHLWINDOW pWindow) const {
    if (!pWindow)
        return false;

    const auto currentWorkspace = workspace();
    if (!currentWorkspace)
        return false;

    for (const auto& nd : m_nodes) {
        auto t = nd.target.lock();
        if (!t || t->window() != pWindow)
            continue;

        s_followWorkspaceMovesByStableID[pWindow->metadata().stableID()] = {
            .zone              = nd.zone,
            .sourceWorkspaceAddress = currentWorkspace->addressableName(),
        };
        return true;
    }

    return false;
}

std::optional<eHyprtrizoneZone> CHyprtrizoneAlgorithm::getPreferredZoneForWindow(PHLWINDOW pWindow) const {
    if (!pWindow)
        return std::nullopt;

    struct ZoneTagConfig {
        const char*            configKey;
        eHyprtrizoneZone zone;
    };

    // Priority order: center > left > right
    static const ZoneTagConfig configs[] = {
        {"plugin:hyprtrizone:zone_tags_center", HYPRTRIZONE_ZONE_CENTER},
        {"plugin:hyprtrizone:zone_tags_left", HYPRTRIZONE_ZONE_LEFT},
        {"plugin:hyprtrizone:zone_tags_right", HYPRTRIZONE_ZONE_RIGHT},
    };

    for (const auto& cfg : configs) {
        const std::string configured = getPluginString(cfg.configKey);
        if (windowHasAnyConfiguredTag(pWindow, configured))
            return cfg.zone;
    }

    return std::nullopt;
}

void CHyprtrizoneAlgorithm::insertNodeAtTopOfZone(std::list<SNodeData>::iterator nodeIt, eHyprtrizoneZone zone) {
    auto insertBefore = m_nodes.end();
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (it == nodeIt)
            continue;

        if (it->zone == zone) {
            insertBefore = it;
            break;
        }
    }

    if (insertBefore != m_nodes.end())
        m_nodes.splice(insertBefore, m_nodes, nodeIt);
}

void CHyprtrizoneAlgorithm::displaceUntaggedCenterWindows() {
    // changeNodeZone reorders m_nodes, so collect first
    std::vector<std::pair<SNodeData*, eHyprtrizoneZone>> displaced;

    for (auto& nd : m_nodes) {
        if (nd.zone != HYPRTRIZONE_ZONE_CENTER)
            continue;

        auto t = nd.target.lock();
        if (!t)
            continue;

        auto w = t->window();
        if (!w)
            continue;

        auto pref = getPreferredZoneForWindow(w);
        if (pref.has_value() && *pref == HYPRTRIZONE_ZONE_CENTER)
            continue; // explicitly tagged for center, keep it

        // Displace: re-evaluate this window's zone
        displaced.emplace_back(&nd, pref.value_or(getBiasZone()));
    }

    for (const auto& [nd, zone] : displaced)
        changeNodeZone(nd, zone);
}

// ─── Pairing helpers ───────────────────────────────────────────────────────

bool CHyprtrizoneAlgorithm::isPairingEnabledForZone(eHyprtrizoneZone zone) const {
    if (zone != HYPRTRIZONE_ZONE_LEFT && zone != HYPRTRIZONE_ZONE_RIGHT)
        return false;

    if (zone == HYPRTRIZONE_ZONE_LEFT && CHyprtrizoneGlobals::pairingLeftOverride.has_value())
        return *CHyprtrizoneGlobals::pairingLeftOverride;
    if (zone == HYPRTRIZONE_ZONE_RIGHT && CHyprtrizoneGlobals::pairingRightOverride.has_value())
        return *CHyprtrizoneGlobals::pairingRightOverride;

    return zone == HYPRTRIZONE_ZONE_LEFT ? getPluginInt("plugin:hyprtrizone:pairing_left", 0) != 0
                                               : getPluginInt("plugin:hyprtrizone:pairing_right", 0) != 0;
}

bool CHyprtrizoneAlgorithm::isWindowPairEligible(PHLWINDOW pWindow, eHyprtrizoneZone zone) const {
    if (!pWindow || !isPairingEnabledForZone(zone))
        return false;

    if (getPluginInt("plugin:hyprtrizone:pairing_tag_only", 1) == 0)
        return true;

    const std::string configured = getPluginString("plugin:hyprtrizone:pairing_tags");
    return windowHasAnyConfiguredTag(pWindow, configured);
}

int CHyprtrizoneAlgorithm::getSideRowCount(eHyprtrizoneZone zone) const {
    return buildSideRows(zone).size();
}

std::vector<CHyprtrizoneAlgorithm::SSideRow> CHyprtrizoneAlgorithm::buildSideRows(eHyprtrizoneZone zone) const {
    std::vector<SSideRow> rows;

    if (zone != HYPRTRIZONE_ZONE_LEFT && zone != HYPRTRIZONE_ZONE_RIGHT)
        return rows;

    for (auto& nd : m_nodes) {
        if (nd.zone != zone)
            continue;

        auto t = nd.target.lock();
        if (!t)
            continue;

        auto w = t->window();
        if (!w)
            continue;

        const bool eligible = isWindowPairEligible(w, zone);

        if (eligible && nd.pairWithPrevious && !rows.empty() && rows.back().first && !rows.back().second) {
            auto firstTarget = rows.back().first->target.lock();
            auto firstWindow = firstTarget ? firstTarget->window() : nullptr;
            if (firstWindow && isWindowPairEligible(firstWindow, zone)) {
                rows.back().second = const_cast<SNodeData*>(&nd);
                continue;
            }
        }

        SSideRow row;
        row.first = const_cast<SNodeData*>(&nd);
        rows.push_back(row);
    }

    return rows;
}

CHyprtrizoneAlgorithm::SNodeData* CHyprtrizoneAlgorithm::getSideRowLeader(SNodeData* pNode) {
    if (!pNode)
        return nullptr;

    if (pNode->zone != HYPRTRIZONE_ZONE_LEFT && pNode->zone != HYPRTRIZONE_ZONE_RIGHT)
        return pNode;

    const auto rows = buildSideRows(pNode->zone);
    for (const auto& row : rows) {
        if (row.first == pNode || row.second == pNode)
            return row.first ? row.first : pNode;
    }

    return pNode;
}

std::list<CHyprtrizoneAlgorithm::SNodeData>::iterator CHyprtrizoneAlgorithm::findNodeIt(SNodeData* pNode) {
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (&(*it) == pNode)
            return it;
    }

    return m_nodes.end();
}

// m_nodes interleaves all zones, so a node's neighbour within its zone is not necessarily adjacent in the list.
std::list<CHyprtrizoneAlgorithm::SNodeData>::iterator CHyprtrizoneAlgorithm::prevNodeInZone(std::list<SNodeData>::iterator it) {
    if (it == m_nodes.end())
        return m_nodes.end();

    for (auto prev = it; prev != m_nodes.begin();) {
        --prev;
        if (prev->zone == it->zone)
            return prev;
    }

    return m_nodes.end();
}

std::list<CHyprtrizoneAlgorithm::SNodeData>::iterator CHyprtrizoneAlgorithm::nextNodeInZone(std::list<SNodeData>::iterator it) {
    if (it == m_nodes.end())
        return m_nodes.end();

    for (auto next = std::next(it); next != m_nodes.end(); ++next) {
        if (next->zone == it->zone)
            return next;
    }

    return m_nodes.end();
}

// Structural pair partner by pair flags, independent of current pair eligibility.
CHyprtrizoneAlgorithm::SNodeData* CHyprtrizoneAlgorithm::getPairPartner(SNodeData* pNode) {
    auto it = findNodeIt(pNode);
    if (it == m_nodes.end() || (it->zone != HYPRTRIZONE_ZONE_LEFT && it->zone != HYPRTRIZONE_ZONE_RIGHT))
        return nullptr;

    if (it->pairWithPrevious) {
        auto prev = prevNodeInZone(it);
        return prev != m_nodes.end() && !prev->pairWithPrevious ? &(*prev) : nullptr;
    }

    auto next = nextNodeInZone(it);
    return next != m_nodes.end() && next->pairWithPrevious ? &(*next) : nullptr;
}

// Must run before a node leaves its row (removal or zone change), otherwise a left-behind
// second member would pair with whatever row ends up above it.
void CHyprtrizoneAlgorithm::detachFromPair(SNodeData* pNode) {
    auto it = findNodeIt(pNode);
    if (it == m_nodes.end())
        return;

    if (auto next = nextNodeInZone(it); next != m_nodes.end())
        next->pairWithPrevious = false;

    it->pairWithPrevious = false;
}

// Moves a node to the end of another zone, following the same pairing rule as a newly opened window.
void CHyprtrizoneAlgorithm::changeNodeZone(SNodeData* pNode, eHyprtrizoneZone zone) {
    auto it = findNodeIt(pNode);
    if (it == m_nodes.end() || it->zone == zone)
        return;

    detachFromPair(pNode);

    it->zone = zone;
    m_nodes.splice(m_nodes.end(), m_nodes, it);

    if (zone == HYPRTRIZONE_ZONE_LEFT || zone == HYPRTRIZONE_ZONE_RIGHT) {
        it->pairWithPrevious = shouldPairWithPrevious(it);
        snapZoneToEnd(zone);
    }
}

std::list<CHyprtrizoneAlgorithm::SNodeData>::iterator CHyprtrizoneAlgorithm::getInsertPositionAfterSideRow(std::list<SNodeData>::iterator anchorIt) {
    if (anchorIt == m_nodes.end())
        return m_nodes.end();

    if (anchorIt->zone != HYPRTRIZONE_ZONE_LEFT && anchorIt->zone != HYPRTRIZONE_ZONE_RIGHT)
        return std::next(anchorIt);

    const auto zone = anchorIt->zone;
    const auto nextInZone = [&](std::list<SNodeData>::iterator it) {
        for (auto next = std::next(it); next != m_nodes.end(); ++next) {
            if (next->zone == zone)
                return next;
        }

        return m_nodes.end();
    };

    auto rowLeader = anchorIt;
    if (anchorIt->pairWithPrevious) {
        for (auto prev = anchorIt; prev != m_nodes.begin();) {
            --prev;
            if (prev->zone == zone) {
                rowLeader = prev;
                break;
            }
        }
    }

    auto rowTail = rowLeader;
    if (auto pairSecond = nextInZone(rowLeader); pairSecond != m_nodes.end() && pairSecond->pairWithPrevious)
        rowTail = pairSecond;

    return nextInZone(rowTail);
}

bool CHyprtrizoneAlgorithm::shouldPairWithPrevious(std::list<SNodeData>::iterator nodeIt) const {
    if (nodeIt == m_nodes.end())
        return false;

    const auto zone = nodeIt->zone;
    if (zone != HYPRTRIZONE_ZONE_LEFT && zone != HYPRTRIZONE_ZONE_RIGHT)
        return false;

    auto prevIt = m_nodes.end();
    for (auto it = nodeIt; it != m_nodes.begin();) {
        --it;
        if (it->zone == zone) {
            prevIt = it;
            break;
        }
    }

    if (prevIt == m_nodes.end())
        return false;

    auto prevTarget = prevIt->target.lock();
    auto prevWindow = prevTarget ? prevTarget->window() : nullptr;
    auto newTarget  = nodeIt->target.lock();
    auto newWindow  = newTarget ? newTarget->window() : nullptr;

    if (!prevWindow || !newWindow)
        return false;

    if (!isWindowPairEligible(prevWindow, zone) || !isWindowPairEligible(newWindow, zone))
        return false;

    if (prevIt->pairWithPrevious)
        return false;

    for (auto nextIt = std::next(prevIt); nextIt != m_nodes.end(); ++nextIt) {
        if (nextIt->zone != zone)
            continue;

        if (nextIt != nodeIt && nextIt->pairWithPrevious)
            return false;

        break;
    }

    return true;
}

void CHyprtrizoneAlgorithm::moveNodeToZoneOrder(std::list<SNodeData>::iterator nodeIt, eHyprtrizoneZone zone, int orderInZone) {
    if (nodeIt == m_nodes.end())
        return;

    nodeIt->zone = zone;

    int  currentZoneIdx = 0;
    auto insertBefore   = m_nodes.end();
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (it == nodeIt)
            continue;

        if (it->zone != zone)
            continue;

        if (currentZoneIdx == orderInZone) {
            insertBefore = it;
            break;
        }

        currentZoneIdx++;
    }

    if (insertBefore != m_nodes.end())
        m_nodes.splice(insertBefore, m_nodes, nodeIt);
}

CHyprtrizoneAlgorithm::SSideLayoutMetrics CHyprtrizoneAlgorithm::getSideLayoutMetrics(eHyprtrizoneZone zone, const std::vector<SSideRow>& rows,
                                                                                                   float workspaceHeight) const {
    SSideLayoutMetrics metrics;

    if (rows.empty() || workspaceHeight <= 0.f)
        return metrics;

    metrics.slotHeight = getSideSlotHeight(zone, workspaceHeight, rows.size());
    if (metrics.slotHeight <= 0.f)
        return metrics;

    std::vector<float> weights;
    weights.reserve(rows.size());

    float totalWeight = 0.f;
    for (const auto& row : rows) {
        const float weight = row.first ? std::max(0.05f, row.first->percSize) : 1.f;
        weights.push_back(weight);
        totalWeight += weight;
    }

    const float normalizeScale = totalWeight > 0.f ? (float)rows.size() / totalWeight : 1.f;

    metrics.rowTops.reserve(rows.size());
    metrics.rowHeights.reserve(rows.size());

    float nextTop = 0.f;
    for (const float weight : weights) {
        metrics.rowTops.push_back(nextTop);
        const float rowHeight = metrics.slotHeight * weight * normalizeScale;
        metrics.rowHeights.push_back(rowHeight);
        nextTop += rowHeight;
    }

    metrics.contentHeight = nextTop;
    return metrics;
}

// ─── Window positioning ────────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::applyNodeDataToWindow(SNodeData* pNode) {
    auto target = pNode->target.lock();
    if (!target)
        return;

    auto PWINDOW = target->window();
    if (!PWINDOW)
        return;

    const auto WORKAREA = getWorkArea();
    if (WORKAREA.w <= 0 || WORKAREA.h <= 0)
        return;

    const auto PMONITOR = getMonitor();
    if (!PMONITOR)
        return;

    const auto PWORKSPACE = workspace();
    if (!PWORKSPACE)
        return;

    PWINDOW->m_ruleApplicator->resetProps(Desktop::Rule::RULE_PROP_ALL, Desktop::Types::PRIORITY_LAYOUT);
    PWINDOW->updateWindowData();

    if (!validMapped(PWINDOW)) {
        Log::logger->log(Log::ERR, "Hyprtrizone: Node holding invalid window!");
        return;
    }

    if (PWINDOW->onSpecialWorkspace()) {
        auto scaledSize = pNode->size * getPluginFloat("plugin:hyprtrizone:special_scale_factor", 0.8F);
        auto scaledPos  = pNode->position + (pNode->size - scaledSize) / 2.f;

        CBox wb = {scaledPos, scaledSize};
        wb.round();
        target->setPositionGlobal(wb);
    } else {
        CBox wb = {pNode->position, pNode->size};
        wb.round();
        target->setPositionGlobal(wb);
    }

    static CConfigValue<Config::INTEGER> animateManualResizes{"misc:animate_manual_resizes"};
    const auto                           animateDisabled = *animateManualResizes == 0;

    if (m_forceWarps && animateDisabled) {
        g_pHyprRenderer->damageWindow(PWINDOW);
        target->warpPositionSize();
        g_pHyprRenderer->damageWindow(PWINDOW);
    }

    PWINDOW->presentation().updateDecorations();
}

// ─── newTarget ─────────────────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::newTarget(SP<Layout::ITarget> target) {
    m_nodes.emplace_back();
    auto  newNodeIt = std::prev(m_nodes.end());
    auto& node      = *newNodeIt;
    node.target     = target;
    node.pairWithPrevious = false;
    node.zone       = HYPRTRIZONE_ZONE_RIGHT; // temporary; prevents getMasterNode() from finding the unassigned node

    const int NODECOUNT = getNodeCount();
    std::optional<SFollowWorkspaceMoveInfo> pendingFollowMove;

    // Prune stale cache entries periodically
    if (s_zoneCacheByStableID.size() > 50) {
        std::unordered_set<uint64_t> liveStableIDs;
        liveStableIDs.reserve(Desktop::windowState()->windows().size());
        for (const auto& w : Desktop::windowState()->windows()) {
            if (w)
                liveStableIDs.insert(w->metadata().stableID());
        }

        std::erase_if(s_zoneCacheByStableID, [&liveStableIDs](const auto& pair) {
            return !liveStableIDs.contains(pair.first);
        });
    }

    // Restore cached zone, pair state, and order from layout swap / workspace move
    if (auto w = target->window()) {
        auto followIt = s_followWorkspaceMovesByStableID.find(w->metadata().stableID());
        if (followIt != s_followWorkspaceMovesByStableID.end()) {
            pendingFollowMove = followIt->second;
            s_followWorkspaceMovesByStableID.erase(followIt);
        }

        auto cacheIt = s_zoneCacheByStableID.find(w->metadata().stableID());
        if (cacheIt != s_zoneCacheByStableID.end()) {
            const auto cached = cacheIt->second;
            s_zoneCacheByStableID.erase(cacheIt);

            const auto currentWorkspace = workspace();
            const bool sameWorkspaceRestore   = currentWorkspace && cached.workspaceAddress == currentWorkspace->addressableName();
            const bool followWorkspaceRestore = pendingFollowMove.has_value() && currentWorkspace && currentWorkspace->addressableName() != pendingFollowMove->sourceWorkspaceAddress &&
                cached.workspaceAddress == pendingFollowMove->sourceWorkspaceAddress;

            node.zone             = followWorkspaceRestore ? pendingFollowMove->zone : cached.zone;
            node.pairWithPrevious = sameWorkspaceRestore ? cached.pairWithPrevious : false;

            if (followWorkspaceRestore) {
                insertNodeAtTopOfZone(newNodeIt, node.zone);
            } else if (sameWorkspaceRestore && cached.restoreBatch != 0) {
                // A layout switch hands over targets in Hyprland's order, not in zone order: place the node
                // among the nodes restored from the same snapshot.
                node.restoreBatch = cached.restoreBatch;
                node.restoreOrder = cached.orderInZone;

                auto insertBefore = m_nodes.end();
                auto lastBefore   = m_nodes.end();
                for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
                    if (it == newNodeIt || it->zone != node.zone || it->restoreBatch != node.restoreBatch)
                        continue;
                    if (it->restoreOrder > node.restoreOrder) {
                        insertBefore = it;
                        break;
                    }
                    lastBefore = it;
                }
                if (insertBefore == m_nodes.end() && lastBefore != m_nodes.end())
                    insertBefore = std::next(lastBefore);
                if (insertBefore != m_nodes.end() && insertBefore != newNodeIt)
                    m_nodes.splice(insertBefore, m_nodes, newNodeIt);
            } else if (sameWorkspaceRestore) {
                // Reposition node to match cached order within its zone for same-workspace restores.
                int currentZoneIdx = 0;
                auto insertBefore  = m_nodes.end();
                for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
                    if (&(*it) == &node)
                        continue;
                    if (it->zone == cached.zone) {
                        if (currentZoneIdx == cached.orderInZone) {
                            insertBefore = it;
                            break;
                        }
                        currentZoneIdx++;
                    }
                }
                if (insertBefore != m_nodes.end() && &(*insertBefore) != &node)
                    m_nodes.splice(insertBefore, m_nodes, newNodeIt);
            }

            if (!followWorkspaceRestore && (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT))
                snapZoneToEnd(node.zone);

            recalculate();

            if (!sameWorkspaceRestore && (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT))
                focusWindowInSideStack(target);

            return;
        }
    }

    // Follow-driven workspace moves should keep their zone even if the
    // destination workspace has no existing master yet.
    if (pendingFollowMove.has_value() && !getMasterNode()) {
        node.zone             = pendingFollowMove->zone;
        node.pairWithPrevious = false;

        if (NODECOUNT > 1)
            insertNodeAtTopOfZone(newNodeIt, node.zone);

        recalculate();

        if (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT)
            focusWindowInSideStack(target);

        return;
    }

    // First window always becomes master
    if (NODECOUNT == 1) {
        node.zone = HYPRTRIZONE_ZONE_CENTER;
        recalculate();
        return;
    }

    // No master: force center
    if (!getMasterNode()) {
        node.zone = HYPRTRIZONE_ZONE_CENTER;
        recalculate();
        return;
    }

    auto findSideAnchorIt = [&](std::optional<eHyprtrizoneZone> requiredZone = std::nullopt) {
        auto matchesSideAnchor = [&](PHLWINDOW candidate) {
            if (!candidate)
                return m_nodes.end();

            for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
                if (it == newNodeIt)
                    continue;

                auto t = it->target.lock();
                if (!t || t->window() != candidate)
                    continue;

                if (it->zone != HYPRTRIZONE_ZONE_LEFT && it->zone != HYPRTRIZONE_ZONE_RIGHT)
                    return m_nodes.end();

                if (requiredZone.has_value() && it->zone != *requiredZone)
                    return m_nodes.end();

                return it;
            }

            return m_nodes.end();
        };

        if (auto it = matchesSideAnchor(Desktop::focusState()->window()); it != m_nodes.end())
            return it;

        const auto cursorPos = g_pInputManager->getMouseCoordsInternal();
        const auto pwindowAt = Desktop::viewState()->hitTest().windowAt(
            cursorPos, Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING, target->window());
        return matchesSideAnchor(pwindowAt);
    };

    auto insertAfterSideAnchor = [&](std::list<SNodeData>::iterator anchorIt) {
        if (anchorIt == m_nodes.end() || anchorIt == newNodeIt || anchorIt->zone != node.zone)
            return;

        auto insertPos = getInsertPositionAfterSideRow(anchorIt);
        m_nodes.splice(insertPos, m_nodes, newNodeIt);
    };

    // Check tag-based preferred zone
    auto preferredZone = getPreferredZoneForWindow(target->window());
    if (preferredZone.has_value()) {
        node.zone = *preferredZone;

        // Displace untagged center windows when a center-tagged window enters
        if (node.zone == HYPRTRIZONE_ZONE_CENTER)
            displaceUntaggedCenterWindows();

        // Preferred side zones still anchor off the focused side row so pairable
        // windows can join that row instead of always appending as a new row.
        if (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT) {
            insertAfterSideAnchor(findSideAnchorIt(node.zone));
            newNodeIt->pairWithPrevious = shouldPairWithPrevious(newNodeIt);
        }

        recalculate();

        if (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT)
            focusWindowInSideStack(target);

        return;
    }

    // Ensure at least one center window exists before applying anchor logic
    if (!getMasterNode()) {
        node.zone = HYPRTRIZONE_ZONE_CENTER;
        recalculate();
        return;
    }

    // Determine zone based on active/anchor window
    auto anchorIt = findSideAnchorIt();

    eHyprtrizoneZone activeZone = getActiveZone();

    if (anchorIt != m_nodes.end())
        node.zone = anchorIt->zone;
    else if (activeZone == HYPRTRIZONE_ZONE_LEFT || activeZone == HYPRTRIZONE_ZONE_RIGHT)
        node.zone = activeZone;
    else
        node.zone = getBiasZone();

    // Insert after anchor
    insertAfterSideAnchor(anchorIt);

    // Check pairing eligibility
    if (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT)
        newNodeIt->pairWithPrevious = shouldPairWithPrevious(newNodeIt);

    recalculate();

    if (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT)
        focusWindowInSideStack(target);
}

// ─── Drop placement ────────────────────────────────────────────────────────

// Places a target that was dropped onto pWindowUnder at dropPos. The target has been removed from the layout
// before (removeTarget), its old slot is in the zone cache. Returns false if the drop was not handled.
bool CHyprtrizoneAlgorithm::placeTargetOnWindow(SP<Layout::ITarget> target, PHLWINDOW pWindowUnder, const Vector2D& dropPos) {
    auto  PWINDOW  = target ? target->window() : nullptr;
    auto* dropNode = pWindowUnder ? getNodeFromWindow(pWindowUnder) : nullptr;
    if (!PWINDOW || !dropNode)
        return false;

    const auto currentWorkspace      = workspace();
    const auto sourceCacheIt         = s_zoneCacheByStableID.find(PWINDOW->metadata().stableID());
    const bool sourceOnThisWorkspace = sourceCacheIt != s_zoneCacheByStableID.end() && currentWorkspace && sourceCacheIt->second.workspaceAddress == currentWorkspace->addressableName();
    const bool draggingLastCenter    = sourceOnThisWorkspace && sourceCacheIt->second.zone == HYPRTRIZONE_ZONE_CENTER && getNodesInZone(HYPRTRIZONE_ZONE_CENTER) == 0;

    auto ensureDraggedNode = [this, &target]() {
        m_nodes.emplace_back();
        auto newNodeIt           = std::prev(m_nodes.end());
        newNodeIt->target        = target;
        newNodeIt->pairWithPrevious = false;
        return newNodeIt;
    };

    const bool sourceInPairRow = sourceOnThisWorkspace && (sourceCacheIt->second.pairWithPrevious || sourceCacheIt->second.pairedWithNext);

    const bool dropInPairRow = getPairPartner(dropNode) != nullptr;

    auto restoreNodeToSourceSlot = [&](std::list<SNodeData>::iterator nodeIt, const SCachedNodeInfo& sourceCache) {
        const auto sourceZone = sourceCache.zone;
        moveNodeToZoneOrder(nodeIt, sourceZone, sourceCache.orderInZone);

        std::vector<std::list<SNodeData>::iterator> sourceZoneNodes;
        sourceZoneNodes.reserve(getNodesInZone(sourceZone));
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            if (it->zone == sourceZone)
                sourceZoneNodes.push_back(it);
        }

        for (size_t i = 0; i < sourceZoneNodes.size(); ++i) {
            if (sourceZoneNodes[i] != nodeIt)
                continue;

            nodeIt->pairWithPrevious = sourceCache.pairWithPrevious;

            if (!sourceCache.pairWithPrevious && sourceInPairRow && i + 1 < sourceZoneNodes.size())
                sourceZoneNodes[i + 1]->pairWithPrevious = true;

            break;
        }
    };

    // Scenario 2: Drop onto center → swap places
    if (dropNode->zone == HYPRTRIZONE_ZONE_CENTER) {
        auto dropTarget = dropNode->target.lock();
        if (!dropTarget)
            return false;

        auto  newNodeIt = ensureDraggedNode();
        auto& node      = *newNodeIt;

        if (sourceCacheIt != s_zoneCacheByStableID.end() &&
            (sourceCacheIt->second.zone == HYPRTRIZONE_ZONE_LEFT || sourceCacheIt->second.zone == HYPRTRIZONE_ZONE_RIGHT)) {
            dropNode->target = target;

            node.target = dropTarget;
            if (sourceOnThisWorkspace)
                restoreNodeToSourceSlot(newNodeIt, sourceCacheIt->second);
            else {
                // Dragged in from another workspace: the old master goes to the end of the same side here.
                node.zone = sourceCacheIt->second.zone;
                snapZoneToEnd(node.zone);
            }

            recalculate();
            return true;
        }

        node.zone = HYPRTRIZONE_ZONE_CENTER;

        // Find dropNode's iterator and insert after it, then swap targets
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            if (&(*it) != dropNode)
                continue;

            m_nodes.splice(std::next(it), m_nodes, newNodeIt);
            std::swap(node.target, dropNode->target);
            break;
        }

        recalculate();
        return true;
    }

    // Dragging the last center window onto another tiled window keeps the
    // dragged window in the drop target's old slot and promotes the drop
    // target to the now-empty center zone.
    if (draggingLastCenter) {
        auto dropTarget = dropNode->target.lock();
        if (!dropTarget)
            return false;

        auto  newNodeIt = ensureDraggedNode();
        auto& node      = *newNodeIt;

        node.zone             = HYPRTRIZONE_ZONE_CENTER;
        node.pairWithPrevious = false;
        std::swap(node.target, dropNode->target);

        recalculate();
        return true;
    }

    // Scenario 3/5: Check pairing — drop onto a side window
    if (dropNode->zone == HYPRTRIZONE_ZONE_LEFT || dropNode->zone == HYPRTRIZONE_ZONE_RIGHT) {
        auto  newNodeIt = ensureDraggedNode();
        auto& node      = *newNodeIt;
        node.zone = dropNode->zone;
        const bool canPairWithDrop = isWindowPairEligible(PWINDOW, node.zone) && isWindowPairEligible(pWindowUnder, node.zone);

        // A full row exchanges, a free slot pairs:
        // - the drop target already has a pair partner: the dragged window takes its slot and the drop target
        //   goes to the dragged window's old slot, nobody else moves;
        // - the drop target is a single row and both windows are pair-eligible: they pair;
        // - otherwise slots are exchanged if the dragged window came out of a pair row, so that row stays intact.
        if (dropInPairRow || (sourceInPairRow && !canPairWithDrop)) {
            auto dropIt     = findNodeIt(dropNode);
            auto dropTarget = dropNode->target.lock();
            if (dropIt != m_nodes.end() && dropTarget) {
                dropNode->target = target;
                node.target      = dropTarget;

                if (sourceOnThisWorkspace)
                    restoreNodeToSourceSlot(newNodeIt, sourceCacheIt->second);
                else {
                    // No old slot on this workspace to go to: the drop target becomes its own row below.
                    auto insertPos = getInsertPositionAfterSideRow(dropIt);
                    if (insertPos != newNodeIt)
                        m_nodes.splice(insertPos, m_nodes, newNodeIt);
                }

                recalculate();
                return true;
            }
        }

        // Try pairing
        if (canPairWithDrop) {
            auto dropIt = findNodeIt(dropNode);
            if (dropIt != m_nodes.end()) {
                // Cursor position decides left/right within the new pair
                auto dropTarget   = dropNode->target.lock();
                bool insertBefore = false;
                if (dropTarget) {
                    auto dropBox = dropTarget->position();
                    insertBefore = dropPos.x < dropBox.middle().x;
                }

                if (insertBefore) {
                    // Dragged becomes left, drop becomes right
                    m_nodes.splice(dropIt, m_nodes, newNodeIt);
                    dropNode->pairWithPrevious = true;
                } else {
                    // Dragged becomes right, drop stays left
                    m_nodes.splice(std::next(dropIt), m_nodes, newNodeIt);
                    newNodeIt->pairWithPrevious = true;
                }
            }

            recalculate();
            return true;
        }

        // Scenario 4: Not pairable — swap places
        // The dragged window takes the drop target's position
        auto dropIt = m_nodes.end();
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            if (&(*it) == dropNode) {
                dropIt = it;
                break;
            }
        }

        if (dropIt != m_nodes.end()) {
            // Insert the new node right after the drop node, then swap targets
            m_nodes.splice(std::next(dropIt), m_nodes, newNodeIt);
            std::swap(node.target, dropNode->target);
            node.pairWithPrevious = dropNode->pairWithPrevious;
            dropNode->pairWithPrevious = false;
        }

        recalculate();
        return true;
    }

    return false;
}

// ─── movedTarget ───────────────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint) {
    const bool isDrag = g_layoutManager->dragController()->wasDraggingWindow() &&
                        g_layoutManager->dragController()->mode() == MBIND_MOVE;

    const auto MOUSECOORDS = g_pInputManager->getMouseCoordsInternal();
    auto       PWINDOW     = target->window();

    // If dragging, check if we dropped onto an existing window in this layout
    if (isDrag && PWINDOW) {
        const auto pWindowUnder = Desktop::viewState()->hitTest().windowAt(
            MOUSECOORDS, Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING, PWINDOW);

        if (pWindowUnder && placeTargetOnWindow(target, pWindowUnder, MOUSECOORDS))
            return;
    }

    // Drag onto empty zone: place in the zone under the cursor
    if (isDrag && PWINDOW) {
        auto cursorZone = getZoneAtCursor();
        const auto sourceCacheIt = s_zoneCacheByStableID.find(PWINDOW->metadata().stableID());
        const bool draggingLastCenter = sourceCacheIt != s_zoneCacheByStableID.end() &&
                                        sourceCacheIt->second.zone == HYPRTRIZONE_ZONE_CENTER &&
                                        getNodesInZone(HYPRTRIZONE_ZONE_CENTER) == 0;

        if (cursorZone == HYPRTRIZONE_ZONE_CENTER && getNodesInZone(HYPRTRIZONE_ZONE_CENTER) == 0) {
            m_nodes.emplace_back();
            auto& node  = m_nodes.back();
            node.target = target;
            node.pairWithPrevious = false;
            node.zone   = HYPRTRIZONE_ZONE_CENTER;
            recalculate();
            return;
        }

        // Don't allow dropping into a side zone if this window is the only master
        bool isSoleMaster = false;
        if (cursorZone != HYPRTRIZONE_ZONE_CENTER) {
            int centerCount = getNodesInZone(HYPRTRIZONE_ZONE_CENTER);
            // The dragged window was removed, so if center has 0 nodes, we shouldn't move away from center
            // But the window might have been from a side zone originally — check if dropping would leave center empty
            isSoleMaster = (centerCount == 0) && !draggingLastCenter;
        }

        if (!isSoleMaster && (cursorZone == HYPRTRIZONE_ZONE_LEFT || cursorZone == HYPRTRIZONE_ZONE_RIGHT)) {
            m_nodes.emplace_back();
            auto& node  = m_nodes.back();
            node.target = target;
            node.pairWithPrevious = false;
            node.zone   = cursorZone;
            snapZoneToEnd(cursorZone);
            recalculate();
            return;
        }
    }

    // Default path: no drag context or no window under cursor
    m_nodes.emplace_back();
    auto  newNodeIt = std::prev(m_nodes.end());
    auto& node      = *newNodeIt;
    node.target = target;
    node.pairWithPrevious = false;

    const int NODECOUNT = getNodeCount();
    std::optional<SFollowWorkspaceMoveInfo> pendingFollowMove;

    // Restore cached zone and order from layout swap / workspace move
    if (auto w = target->window()) {
        auto followIt = s_followWorkspaceMovesByStableID.find(w->metadata().stableID());
        if (followIt != s_followWorkspaceMovesByStableID.end()) {
            pendingFollowMove = followIt->second;
            s_followWorkspaceMovesByStableID.erase(followIt);
        }

        auto cacheIt = s_zoneCacheByStableID.find(w->metadata().stableID());
        if (cacheIt != s_zoneCacheByStableID.end()) {
            const auto cached = cacheIt->second;
            s_zoneCacheByStableID.erase(cacheIt);

            const auto currentWorkspace = workspace();
            const bool sameWorkspaceRestore   = currentWorkspace && cached.workspaceAddress == currentWorkspace->addressableName();
            const bool followWorkspaceRestore = pendingFollowMove.has_value() && currentWorkspace && currentWorkspace->addressableName() != pendingFollowMove->sourceWorkspaceAddress &&
                cached.workspaceAddress == pendingFollowMove->sourceWorkspaceAddress;

            node.zone             = followWorkspaceRestore ? pendingFollowMove->zone : cached.zone;
            node.pairWithPrevious = sameWorkspaceRestore ? cached.pairWithPrevious : false;

            if (followWorkspaceRestore)
                insertNodeAtTopOfZone(newNodeIt, node.zone);
            else if (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT)
                snapZoneToEnd(node.zone);

            recalculate();

            if (!sameWorkspaceRestore && (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT))
                focusWindowInSideStack(target);

            return;
        }
    }

    if (pendingFollowMove.has_value() && !getMasterNode()) {
        node.zone             = pendingFollowMove->zone;
        node.pairWithPrevious = false;

        if (NODECOUNT > 1)
            insertNodeAtTopOfZone(newNodeIt, node.zone);

        recalculate();

        if (node.zone == HYPRTRIZONE_ZONE_LEFT || node.zone == HYPRTRIZONE_ZONE_RIGHT)
            focusWindowInSideStack(target);

        return;
    }

    if (NODECOUNT == 1 || !getMasterNode()) {
        node.zone = HYPRTRIZONE_ZONE_CENTER;
        recalculate();
        return;
    }

    // Check tag-based preferred zone
    auto preferredZone = getPreferredZoneForWindow(target->window());
    if (preferredZone.has_value()) {
        node.zone = *preferredZone;
    } else {
        // Determine zone from cursor position
        node.zone = getZoneAtCursor();

        // If center zone and we already have a master, use bias
        if (node.zone == HYPRTRIZONE_ZONE_CENTER && getMasterNode())
            node.zone = getBiasZone();
    }

    snapZoneToEnd(node.zone);
    recalculate();
}

// ─── removeTarget ──────────────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::removeTarget(SP<Layout::ITarget> target) {
    auto pNode = getNodeFromTarget(target);
    if (!pNode)
        return;

    const bool isDraggedTarget = g_layoutManager->dragController()->target() == target &&
                                 g_layoutManager->dragController()->mode() == MBIND_MOVE;

    auto PWINDOW = target->window();
    if (PWINDOW) {
        PWINDOW->m_ruleApplicator->resetProps(Desktop::Rule::RULE_PROP_ALL, Desktop::Types::PRIORITY_LAYOUT);
        PWINDOW->updateWindowData();

        if (Fullscreen::controller()->isFullscreen(PWINDOW))
            Fullscreen::controller()->setFullscreenMode(PWINDOW, Fullscreen::FSMODE_NONE);
    }

    const auto CURRENT_WORKSPACE = workspace();
    const auto CURRENT_WORKSPACE_ADDRESS = CURRENT_WORKSPACE ? CURRENT_WORKSPACE->addressableName() : std::string{};

    // Hyprland drops a target from its own list before calling removeTarget, except when it hands every
    // target to another layout (CAlgorithm::updateTiledAlgo). Snapshot the layout once, then leave the
    // remaining nodes alone: promoting or unpairing them here would be cached as their state.
    const auto PARENT = m_parent.lock();
    if (m_layoutTeardown || (!m_internalRemoval && PARENT && PARENT->tiledTargets() >= m_nodes.size())) {
        if (!m_layoutTeardown) {
            m_layoutTeardown = true;

            const auto BATCH  = ++s_lastRestoreBatch;
            int        leftIdx = 0, centerIdx = 0, rightIdx = 0;
            for (auto& nd : m_nodes) {
                auto t = nd.target.lock();
                auto w = t ? t->window() : nullptr;
                if (!w)
                    continue;

                int& idx = (nd.zone == HYPRTRIZONE_ZONE_LEFT) ? leftIdx
                         : (nd.zone == HYPRTRIZONE_ZONE_CENTER) ? centerIdx
                                                                       : rightIdx;
                const bool pairedWithNext = !nd.pairWithPrevious && getPairPartner(&nd);
                s_zoneCacheByStableID[w->metadata().stableID()] = {nd.zone, nd.pairWithPrevious, idx++, CURRENT_WORKSPACE_ADDRESS, pairedWithNext, BATCH};
            }
        }

        m_nodes.remove(*pNode);
        return;
    }

    // Cache ALL remaining nodes' zone, pair state, and order for layout-swap restore.
    // Done for all nodes at once so order indices are correct before any removals shift positions.
    // Already-cached nodes (from a previous removeTarget in this batch) are skipped.
    {
        int leftIdx = 0, centerIdx = 0, rightIdx = 0;
        for (const auto& nd : m_nodes) {
            auto t = nd.target.lock();
            if (!t)
                continue;
            auto w = t->window();
            if (!w)
                continue;
            if (s_zoneCacheByStableID.contains(w->metadata().stableID())) {
                if (nd.zone == HYPRTRIZONE_ZONE_LEFT) leftIdx++;
                else if (nd.zone == HYPRTRIZONE_ZONE_CENTER) centerIdx++;
                else rightIdx++;
                continue;
            }
            int& idx = (nd.zone == HYPRTRIZONE_ZONE_LEFT) ? leftIdx
                     : (nd.zone == HYPRTRIZONE_ZONE_CENTER) ? centerIdx
                                                                   : rightIdx;
            s_zoneCacheByStableID[w->metadata().stableID()] = {nd.zone, nd.pairWithPrevious, idx, CURRENT_WORKSPACE_ADDRESS};
            idx++;
        }
    }

    // Always overwrite the removed target's own cache entry with its latest zone.
    // This keeps cross-workspace moves accurate even if an older cache entry exists.
    if (PWINDOW) {
        int orderInZone = 0;
        for (const auto& nd : m_nodes) {
            if (&nd == pNode)
                break;
            if (nd.zone == pNode->zone)
                orderInZone++;
        }

        // Pair membership is recorded here rather than derived from other windows' entries, which may be older.
        const bool pairedWithNext = !pNode->pairWithPrevious && getPairPartner(pNode);
        s_zoneCacheByStableID[PWINDOW->metadata().stableID()] = {pNode->zone, pNode->pairWithPrevious, orderInZone, CURRENT_WORKSPACE_ADDRESS, pairedWithNext};
    }

    // Save focus candidates for getNextCandidate (node is already gone when it's called)
    m_lastRemovedPairPartner.reset();
    m_lastRemovedNextInZone.reset();
    m_lastRemovedPrevInZone.reset();
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (&(*it) != pNode)
            continue;

        // Pair partner
        if (auto* partner = getPairPartner(pNode))
            m_lastRemovedPairPartner = partner->target;

        // Next in same zone
        for (auto fwd = std::next(it); fwd != m_nodes.end(); ++fwd) {
            if (fwd->zone == pNode->zone) {
                m_lastRemovedNextInZone = fwd->target;
                break;
            }
        }

        // Previous in same zone
        for (auto rev = it; rev != m_nodes.begin();) {
            --rev;
            if (rev->zone == pNode->zone) {
                m_lastRemovedPrevInZone = rev->target;
                break;
            }
        }

        break;
    }

    const bool wasCenter = (pNode->zone == HYPRTRIZONE_ZONE_CENTER);
    const bool suppressAutoPromotion = isDraggedTarget && wasCenter && getNodesInZone(HYPRTRIZONE_ZONE_CENTER) == 1;

    // Clean up pairing: if the next node in the same zone was paired with this one, unpair it
    detachFromPair(pNode);

    m_nodes.remove(*pNode);

    // Promote a side window to center if needed
    if (!suppressAutoPromotion && wasCenter && getNodeCount() > 0 && getNodesInZone(HYPRTRIZONE_ZONE_CENTER) == 0) {
        SNodeData*             promoted  = nullptr;
        const auto             BIASZONE  = getBiasZone();
        const eHyprtrizoneZone OTHERZONE = BIASZONE == HYPRTRIZONE_ZONE_LEFT ? HYPRTRIZONE_ZONE_RIGHT : HYPRTRIZONE_ZONE_LEFT;
        const auto             canAutoPromote = [this](SNodeData& nd) {
            auto t = nd.target.lock();
            if (!t)
                return false;

            auto w = t->window();
            if (!w)
                return false;

            return !shouldWindowFollowWorkspaces(w);
        };

        for (auto& nd : m_nodes) {
            if (nd.zone == BIASZONE && canAutoPromote(nd)) {
                promoted = &nd;
                break;
            }
        }
        if (!promoted) {
            for (auto& nd : m_nodes) {
                if (nd.zone == OTHERZONE && canAutoPromote(nd)) {
                    promoted = &nd;
                    break;
                }
            }
        }
        if (promoted)
            promoted->zone = HYPRTRIZONE_ZONE_CENTER;
    }

    clampScrollOffsets();
    recalculate();
}

// ─── recalculate ───────────────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::recalculate(Layout::eRecalculateReason) {
    const auto WORKAREA = getWorkArea();
    if (WORKAREA.w <= 0 || WORKAREA.h <= 0)
        return;

    const auto PMONITOR = getMonitor();
    if (!PMONITOR)
        return;

    g_pHyprRenderer->damageMonitor(PMONITOR);

    // Same as the built-in layouts: a covering fullscreen window is placed by the fullscreen handler.
    if (Fullscreen::controller()->hasFullscreen(workspace(), true)) {
        m_defaultFullscreenHandler->syncTargetSizeAndPosition();
        return;
    }

    if (getNodeCount() == 0)
        return;

    const auto leftRows  = buildSideRows(HYPRTRIZONE_ZONE_LEFT);
    const auto rightRows = buildSideRows(HYPRTRIZONE_ZONE_RIGHT);

    const auto leftBox      = getZoneBox(HYPRTRIZONE_ZONE_LEFT);
    const auto centerBox    = getZoneBox(HYPRTRIZONE_ZONE_CENTER);
    const auto rightBox     = getZoneBox(HYPRTRIZONE_ZONE_RIGHT);
    const auto leftContent  = getZoneContentBox(HYPRTRIZONE_ZONE_LEFT);
    const auto rightContent = getZoneContentBox(HYPRTRIZONE_ZONE_RIGHT);

    clampScrollOffsets();

    // Layout center zone
    int centerCount = getNodesInZone(HYPRTRIZONE_ZONE_CENTER);
    {
        float heightLeft  = centerBox.h;
        int   centersLeft = centerCount;
        float nextY       = centerBox.y;

        for (auto& nd : m_nodes) {
            if (nd.zone != HYPRTRIZONE_ZONE_CENTER)
                continue;

            float h = centersLeft > 1 ? heightLeft / centersLeft * nd.percSize : heightLeft;
            if (h > heightLeft * 0.9f && centersLeft > 1)
                h = heightLeft * 0.9f;

            nd.position = Vector2D(centerBox.x, nextY);
            nd.size     = Vector2D(centerBox.w, h);
            applyNodeDataToWindow(&nd);

            centersLeft--;
            heightLeft -= h;
            nextY += h;
        }
    }

    // Layout left zone
    if (!leftRows.empty() && leftContent.h > 0) {
        const auto  metrics    = getSideLayoutMetrics(HYPRTRIZONE_ZONE_LEFT, leftRows, leftContent.h);
        const float offset     = std::clamp(m_leftScrollOffset, 0.f, std::max(0.f, metrics.contentHeight - (float)leftContent.h));

        float nextY = leftContent.y - offset;
        for (int index = 0; index < (int)leftRows.size(); ++index) {
            auto* const first   = leftRows[index].first;
            auto* const second  = leftRows[index].second;
            if (!first)
                continue;

            const float rowH = index < (int)metrics.rowHeights.size() ? metrics.rowHeights[index] : 0.f;
            if (rowH <= 0.f)
                continue;

            if (!second) {
                first->position = Vector2D(leftContent.x, nextY);
                first->size     = Vector2D(leftContent.w, rowH);
                applyNodeDataToWindow(first);
            } else {
                const float halfWidth = leftContent.w / 2.f;
                first->position       = Vector2D(leftContent.x, nextY);
                first->size           = Vector2D(halfWidth, rowH);
                second->position      = Vector2D(leftContent.x + halfWidth, nextY);
                second->size          = Vector2D(halfWidth, rowH);
                applyNodeDataToWindow(first);
                applyNodeDataToWindow(second);
            }
            nextY += rowH;
        }
    }

    // Layout right zone
    if (!rightRows.empty() && rightContent.h > 0) {
        const auto  metrics    = getSideLayoutMetrics(HYPRTRIZONE_ZONE_RIGHT, rightRows, rightContent.h);
        const float offset     = std::clamp(m_rightScrollOffset, 0.f, std::max(0.f, metrics.contentHeight - (float)rightContent.h));

        float nextY = rightContent.y - offset;
        for (int index = 0; index < (int)rightRows.size(); ++index) {
            auto* const first   = rightRows[index].first;
            auto* const second  = rightRows[index].second;
            if (!first)
                continue;

            const float rowH = index < (int)metrics.rowHeights.size() ? metrics.rowHeights[index] : 0.f;
            if (rowH <= 0.f)
                continue;

            if (!second) {
                first->position = Vector2D(rightContent.x, nextY);
                first->size     = Vector2D(rightContent.w, rowH);
                applyNodeDataToWindow(first);
            } else {
                const float halfWidth = rightContent.w / 2.f;
                first->position       = Vector2D(rightContent.x, nextY);
                first->size           = Vector2D(halfWidth, rowH);
                second->position      = Vector2D(rightContent.x + halfWidth, nextY);
                second->size          = Vector2D(halfWidth, rowH);
                applyNodeDataToWindow(first);
                applyNodeDataToWindow(second);
            }
            nextY += rowH;
        }
    }
}

// ─── swapTargets ───────────────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) {
    auto nodeA = getNodeFromTarget(a);
    auto nodeB = getNodeFromTarget(b);
    if (!nodeA && !nodeB)
        return;

    // Grouping and ungrouping swap a node's target with one outside the layout; the node adopts it
    if (nodeA)
        nodeA->target = b;
    if (nodeB)
        nodeB->target = a;

    auto winA = a->window();
    auto winB = b->window();
    if (winA)
        g_pHyprRenderer->damageWindow(winA);
    if (winB)
        g_pHyprRenderer->damageWindow(winB);

    recalculate();
}

// ─── moveTargetInDirection ─────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection dir, bool silent) {
    auto node = getNodeFromTarget(t);
    if (!node)
        return;

    // Rows as rendered, so movement follows what is on screen
    const auto rows     = buildSideRows(node->zone);
    int        rowIndex = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (rows[i].first == node || rows[i].second == node) {
            rowIndex = i;
            break;
        }
    }

    if (dir == Math::DIRECTION_LEFT || dir == Math::DIRECTION_RIGHT) {
        // Inside a pair row, moving towards the partner exchanges the two members
        if (rowIndex >= 0 && rows[rowIndex].second) {
            const bool isLeftMember = rows[rowIndex].first == node;
            if (isLeftMember == (dir == Math::DIRECTION_RIGHT)) {
                std::swap(rows[rowIndex].first->target, rows[rowIndex].second->target);
                recalculate();
                return;
            }
        }

        eHyprtrizoneZone targetZone = node->zone;

        if (dir == Math::DIRECTION_LEFT) {
            if (node->zone == HYPRTRIZONE_ZONE_RIGHT)
                targetZone = HYPRTRIZONE_ZONE_CENTER;
            else if (node->zone == HYPRTRIZONE_ZONE_CENTER)
                targetZone = HYPRTRIZONE_ZONE_LEFT;
        } else {
            if (node->zone == HYPRTRIZONE_ZONE_LEFT)
                targetZone = HYPRTRIZONE_ZONE_CENTER;
            else if (node->zone == HYPRTRIZONE_ZONE_CENTER)
                targetZone = HYPRTRIZONE_ZONE_RIGHT;
        }

        if (targetZone != node->zone) {
            changeNodeZone(node, targetZone);
            recalculate();
        }
    } else if (dir == Math::DIRECTION_UP || dir == Math::DIRECTION_DOWN) {
        // Find adjacent node in the same zone
        SNodeData* adjacent = nullptr;

        if (rowIndex >= 0) {
            // Side stacks move by row, keeping the column where the neighbouring row is a pair as well
            const int adjacentIndex = rowIndex + (dir == Math::DIRECTION_DOWN ? 1 : -1);
            if (adjacentIndex >= 0 && adjacentIndex < (int)rows.size()) {
                const auto& adjacentRow = rows[adjacentIndex];
                adjacent                = rows[rowIndex].second == node && adjacentRow.second ? adjacentRow.second : adjacentRow.first;
            }
        } else if (dir == Math::DIRECTION_DOWN) {
            bool found = false;
            for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
                if (&(*it) == node) {
                    found = true;
                    continue;
                }
                if (found && it->zone == node->zone) {
                    adjacent = &(*it);
                    break;
                }
            }
        } else {
            SNodeData* prevInZone = nullptr;
            for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
                if (&(*it) == node)
                    break;
                if (it->zone == node->zone)
                    prevInZone = &(*it);
            }
            adjacent = prevInZone;
        }

        if (adjacent) {
            std::swap(node->target, adjacent->target);
            recalculate();
        }
    }
}

// ─── resizeTarget ──────────────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::resizeTarget(const Vector2D& pixResize, SP<Layout::ITarget> target, Layout::eRectCorner corner) {
    auto PWINDOW = target ? target->window() : nullptr;
    if (!PWINDOW)
        return;

    auto pNode = getNodeFromTarget(target);
    if (!pNode) {
        *PWINDOW->sizeAnimation() = Vector2D(std::max((PWINDOW->sizeAnimation()->goal() + pixResize).x, 20.0),
                                              std::max((PWINDOW->sizeAnimation()->goal() + pixResize).y, 20.0));
        PWINDOW->presentation().updateDecorations();
        return;
    }

    const auto PMONITOR = getMonitor();
    if (!PMONITOR)
        return;

    m_forceWarps = true;

    if (pixResize.x != 0) {
        double delta = pixResize.x / PMONITOR->m_size.x;

        if (pNode->zone == HYPRTRIZONE_ZONE_CENTER) {
            m_masterFactor = std::clamp(m_masterFactor + (float)delta, 0.1f, 0.9f);
            m_masterResized = true;
        } else if (pNode->zone == HYPRTRIZONE_ZONE_LEFT) {
            m_masterFactor = std::clamp(m_masterFactor - (float)delta, 0.1f, 0.9f);
            m_masterResized = true;
        } else {
            m_masterFactor = std::clamp(m_masterFactor + (float)delta, 0.1f, 0.9f);
            m_masterResized = true;
        }
    }

    if (pixResize.y != 0) {
        if (pNode->zone == HYPRTRIZONE_ZONE_CENTER) {
            int nodesInZone = getNodesInZone(HYPRTRIZONE_ZONE_CENTER);
            if (nodesInZone > 1) {
                const float WORKAREA = getWorkArea().h;
                const float SIZE     = WORKAREA / nodesInZone;
                pNode->percSize      = std::clamp(pNode->percSize + (float)(pixResize.y / SIZE), 0.05f, 1.95f);
            }
        } else {
            int rowCount = getSideRowCount(pNode->zone);
            if (rowCount > 0) {
                const float baseSlotH = getSideSlotHeight(pNode->zone, getZoneContentBox(pNode->zone).h, rowCount);
                auto* const rowLeader = getSideRowLeader(pNode);
                if (baseSlotH > 0.f && rowLeader)
                    rowLeader->percSize = std::clamp(rowLeader->percSize + (float)(pixResize.y / baseSlotH), 0.05f, 1.95f);
            }
        }
    }

    recalculate();

    m_forceWarps = false;
}

// ─── getNextCandidate ──────────────────────────────────────────────────────

SP<Layout::ITarget> CHyprtrizoneAlgorithm::getNextCandidate(SP<Layout::ITarget> old) {
    // The node for 'old' is already removed by removeTarget before this is called.
    // Use candidates saved during removeTarget.

    // 1. Pair partner
    if (auto t = m_lastRemovedPairPartner.lock())
        return t;

    // 2. Next in same zone
    if (auto t = m_lastRemovedNextInZone.lock())
        return t;

    // 3. Previous in same zone
    if (auto t = m_lastRemovedPrevInZone.lock())
        return t;

    // 4. Fallback: any remaining window
    for (auto& nd : m_nodes) {
        auto t = nd.target.lock();
        if (t && t != old)
            return t;
    }

    return nullptr;
}

// ─── layoutMsg ─────────────────────────────────────────────────────────────

Config::ErrorResult CHyprtrizoneAlgorithm::layoutMsg(const std::string_view& sv) {
    auto switchToWindow = [&](PHLWINDOW PWINDOWTOCHANGETO) {
        if (!validMapped(PWINDOWTOCHANGETO))
            return;

        Desktop::focusState()->fullWindowFocus(PWINDOWTOCHANGETO, Desktop::FOCUS_REASON_KEYBIND);
        Pointer::pointerController()->warpTo(PWINDOWTOCHANGETO->middle());

        g_pInputManager->m_forcedFocus = PWINDOWTOCHANGETO;
        g_pInputManager->simulateMouseMovement();
        g_pInputManager->m_forcedFocus.reset();
    };

    CVarList2 vars(std::string(sv), 0, ' ');

    if (vars.size() < 1 || vars[0].empty())
        return Config::configError("hyprtrizone layoutmsg called without params", Config::eConfigErrorLevel::WARNING, Config::eConfigErrorCode::INVALID_ARGUMENT);

    auto command = vars[0];

    // ── Global toggles (affect all workspaces) ──

    if (command == "pairingleft" || command == "pairingright") {
        if (vars.size() < 2)
            return {};

        const bool isLeft    = command == "pairingleft";
        auto&      overrideRef = isLeft ? CHyprtrizoneGlobals::pairingLeftOverride : CHyprtrizoneGlobals::pairingRightOverride;
        auto       arg       = vars[1];

        if (arg == "on" || arg == "1" || arg == "true")
            overrideRef = true;
        else if (arg == "off" || arg == "0" || arg == "false")
            overrideRef = false;
        else if (arg == "toggle") {
            const auto zone = isLeft ? HYPRTRIZONE_ZONE_LEFT : HYPRTRIZONE_ZONE_RIGHT;
            overrideRef     = !isPairingEnabledForZone(zone);
        } else if (arg == "default" || arg == "config")
            overrideRef.reset();
        else
            return {};

        for (auto& m : State::monitorState()->monitors()) {
            if (m)
                g_layoutManager->recalculateMonitor(m);
        }

        return {};
    }

    if (command == "togglescrolling") {
        const bool newValue = !CHyprtrizoneGlobals::isScrollingEnabled();
        CHyprtrizoneGlobals::scrollingEnabledOverride = newValue;

        if (!newValue) {
            for (auto* inst : instances()) {
                inst->m_leftScrollOffset  = 0.f;
                inst->m_rightScrollOffset = 0.f;
            }
        }

        for (auto& m : State::monitorState()->monitors()) {
            if (m)
                g_layoutManager->recalculateMonitor(m);
        }

        return {};
    }

    if (command == "setscrolling") {
        if (vars.size() < 2)
            return {};

        auto arg = vars[1];
        if (arg == "1" || arg == "on" || arg == "true")
            CHyprtrizoneGlobals::scrollingEnabledOverride = true;
        else if (arg == "0" || arg == "off" || arg == "false")
            CHyprtrizoneGlobals::scrollingEnabledOverride = false;
        else if (arg == "default" || arg == "config")
            CHyprtrizoneGlobals::scrollingEnabledOverride.reset();
        else
            return {};

        if (!CHyprtrizoneGlobals::isScrollingEnabled()) {
            for (auto* inst : instances()) {
                inst->m_leftScrollOffset  = 0.f;
                inst->m_rightScrollOffset = 0.f;
            }
        }

        for (auto& m : State::monitorState()->monitors()) {
            if (m)
                g_layoutManager->recalculateMonitor(m);
        }

        return {};
    }

    // ── Per-window commands (need focused target) ──

    if (command == "scroll" || command == "scrollup" || command == "scrolldown") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode)
            return {};

        eHyprtrizoneZone targetZone = pNode->zone;
        float                  direction  = 0.f;
        float                  amount     = 1.f;

        auto parseDir = [&](std::string_view v) {
            if (v == "up")
                return -1.f;
            if (v == "down")
                return 1.f;
            return 0.f;
        };

        if (command == "scrollup")
            direction = -1.f;
        else if (command == "scrolldown")
            direction = 1.f;

        bool explicitZone = false;

        if (command == "scroll") {
            size_t idx = 1;
            if (vars.size() > idx && (vars[idx] == "left" || vars[idx] == "right")) {
                targetZone   = vars[idx] == "left" ? HYPRTRIZONE_ZONE_LEFT : HYPRTRIZONE_ZONE_RIGHT;
                explicitZone = true;
                idx++;
            }

            if (vars.size() > idx) {
                const auto parsedDir = parseDir(vars[idx]);
                if (parsedDir != 0.f) {
                    direction = parsedDir;
                    idx++;
                }
            }

            if (vars.size() > idx) {
                try {
                    amount = std::stof(std::string(vars[idx]));
                } catch (const std::exception&) { amount = 1.f; }
            }
        } else if (vars.size() > 1) {
            if (vars[1] == "left" || vars[1] == "right")
                targetZone = vars[1] == "left" ? HYPRTRIZONE_ZONE_LEFT : HYPRTRIZONE_ZONE_RIGHT;
            else {
                try {
                    amount = std::stof(std::string(vars[1]));
                } catch (const std::exception&) { amount = 1.f; }
            }
            explicitZone = (vars[1] == "left" || vars[1] == "right");
        }

        if (!explicitZone && targetZone == HYPRTRIZONE_ZONE_CENTER)
            targetZone = getBiasZone();

        if (direction != 0.f)
            scrollZone(targetZone, direction, amount);

        return {};
    }

    if (command == "windowfocus") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        focusWindowInSideStack(focusedTarget);
        return {};
    }

    if (command == "movetozone") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode)
            return {};

        if (vars.size() < 2)
            return {};

        auto target = vars[1];

        if (target == "left") {
            if (pNode->zone == HYPRTRIZONE_ZONE_LEFT)
                return {};
            changeNodeZone(pNode, HYPRTRIZONE_ZONE_LEFT);
        } else if (target == "right") {
            if (pNode->zone == HYPRTRIZONE_ZONE_RIGHT)
                return {};
            changeNodeZone(pNode, HYPRTRIZONE_ZONE_RIGHT);
        } else if (target == "center") {
            if (pNode->zone == HYPRTRIZONE_ZONE_CENTER)
                return {};
            changeNodeZone(pNode, HYPRTRIZONE_ZONE_CENTER);
        }

        recalculate();
        return {};
    }

    // dropon <source> <target> [left|right]: place source as if it had been dragged onto target, with the
    // cursor on the given half of it. Runs the same remove/place sequence as a mouse drag.
    if (command == "dropon") {
        if (vars.size() < 3)
            return Config::configError("hyprtrizone dropon: expected <source> <target> [left|right]", Config::eConfigErrorLevel::WARNING,
                                       Config::eConfigErrorCode::INVALID_ARGUMENT);

        const auto sourceWindow = Desktop::viewState()->query().selector(std::string{vars[1]}).runWindow();
        const auto dropWindow   = Desktop::viewState()->query().selector(std::string{vars[2]}).runWindow();
        auto*      sourceNode   = sourceWindow ? getNodeFromWindow(sourceWindow) : nullptr;
        auto*      dropNode     = dropWindow ? getNodeFromWindow(dropWindow) : nullptr;
        if (!sourceNode || !dropNode || sourceNode == dropNode)
            return Config::configError("hyprtrizone dropon: source and target must be two windows of this layout", Config::eConfigErrorLevel::WARNING,
                                       Config::eConfigErrorCode::INVALID_ARGUMENT);

        // A real drag keeps the center empty while its last window is in the air; removeTarget would promote here.
        if (sourceNode->zone == HYPRTRIZONE_ZONE_CENTER && getNodesInZone(HYPRTRIZONE_ZONE_CENTER) == 1)
            return Config::configError("hyprtrizone dropon: cannot move the only center window", Config::eConfigErrorLevel::WARNING,
                                       Config::eConfigErrorCode::INVALID_STATE);

        auto sourceTarget = sourceNode->target.lock();
        if (!sourceTarget)
            return {};

        m_internalRemoval = true;
        removeTarget(sourceTarget);
        m_internalRemoval = false;

        // As with a drag, the half is judged on the target's box after the source has left the layout.
        auto       dropTarget = dropNode->target.lock();
        const auto dropBox    = dropTarget ? dropTarget->position() : CBox{};
        const bool leftHalf   = vars.size() >= 4 && vars[3] == "left";
        const auto dropPos    = Vector2D{dropBox.x + dropBox.w * (leftHalf ? 0.25 : 0.75), dropBox.y + dropBox.h / 2.0};

        if (!placeTargetOnWindow(sourceTarget, dropWindow, dropPos))
            movedTarget(sourceTarget, std::nullopt);

        return {};
    }

    if (command == "swapzone") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode)
            return {};

        if (pNode->zone == HYPRTRIZONE_ZONE_LEFT)
            changeNodeZone(pNode, HYPRTRIZONE_ZONE_RIGHT);
        else if (pNode->zone == HYPRTRIZONE_ZONE_RIGHT)
            changeNodeZone(pNode, HYPRTRIZONE_ZONE_LEFT);

        recalculate();
        return {};
    }

    if (command == "swapwithmaster") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode)
            return {};

        if (pNode->zone == HYPRTRIZONE_ZONE_CENTER) {
            for (auto& nd : m_nodes) {
                if (nd.zone != HYPRTRIZONE_ZONE_CENTER) {
                    auto t = nd.target.lock();
                    if (!t)
                        continue;
                    auto win = t->window();
                    if (win && validMapped(win)) {
                        switchToWindow(win);
                    }
                    return {};
                }
            }
            return {};
        }

        auto PMASTER = getMasterNode();
        if (!PMASTER)
            return {};

        // Exchange the windows rather than the nodes so the old master takes over the side slot as is,
        // including its place in a pair row.
        std::swap(PMASTER->target, pNode->target);

        recalculate();
        return {};
    }

    if (command == "promote") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode || pNode->zone == HYPRTRIZONE_ZONE_CENTER)
            return {};

        // changeNodeZone reorders m_nodes, so collect first
        std::vector<SNodeData*> centerNodes;
        for (auto& nd : m_nodes) {
            if (nd.zone == HYPRTRIZONE_ZONE_CENTER)
                centerNodes.push_back(&nd);
        }

        for (auto* nd : centerNodes)
            changeNodeZone(nd, getBiasZone());

        changeNodeZone(pNode, HYPRTRIZONE_ZONE_CENTER);

        recalculate();
        return {};
    }

    if (command == "addmaster") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode || pNode->zone == HYPRTRIZONE_ZONE_CENTER)
            return {};

        changeNodeZone(pNode, HYPRTRIZONE_ZONE_CENTER);
        recalculate();
        return {};
    }

    if (command == "removemaster") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode || pNode->zone != HYPRTRIZONE_ZONE_CENTER)
            return {};

        if (getNodesInZone(HYPRTRIZONE_ZONE_CENTER) <= 1)
            return {};

        changeNodeZone(pNode, getBiasZone());
        recalculate();
        return {};
    }

    if (command == "focusmaster") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto PMASTER = getMasterNode();
        if (!PMASTER)
            return {};

        auto masterTarget = PMASTER->target.lock();
        if (!masterTarget || masterTarget == focusedTarget)
            return {};

        auto masterWin = masterTarget->window();
        if (masterWin)
            switchToWindow(masterWin);

        return {};
    }

    if (command == "rezone") {
        auto focusedTarget = getFocusedTarget();
        if (!focusedTarget)
            return {};

        auto pNode = getNodeFromTarget(focusedTarget);
        if (!pNode)
            return {};

        auto pWindow = focusedTarget->window();
        if (!pWindow)
            return {};

        auto preferredZone = getPreferredZoneForWindow(pWindow);
        if (!preferredZone.has_value())
            return {};

        if (pNode->zone == *preferredZone)
            return {};

        changeNodeZone(pNode, *preferredZone);

        // If moving to center, displace untagged center windows
        if (pNode->zone == HYPRTRIZONE_ZONE_CENTER)
            displaceUntaggedCenterWindows();

        recalculate();
        return {};
    }

    return {};
}

// ─── predictSizeForNewTarget ───────────────────────────────────────────────

std::optional<Vector2D> CHyprtrizoneAlgorithm::predictSizeForNewTarget() {
    return std::nullopt;
}


// ─── renderStackIndicators ─────────────────────────────────────────────────

void CHyprtrizoneAlgorithm::renderStackIndicators(PHLMONITOR PMONITOR, Render::CRenderContext& ctx) {
    if (!PMONITOR)
        return;

    const auto WORKAREA = getWorkArea();
    if (WORKAREA.w <= 0 || WORKAREA.h <= 0)
        return;

    static const auto FULL_REGION = CRegion{0, 0, INT32_MAX, INT32_MAX};

    Render::GL::g_pHyprOpenGL->setCapStatus(GL_STENCIL_TEST, false);
    Render::GL::g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    Render::GL::g_pHyprOpenGL->scissor(ctx, static_cast<const pixman_box32*>(nullptr));

    const auto indicatorWidth       = getPluginInt("plugin:hyprtrizone:indicator_width", 112);
    const auto indicatorHeight      = getPluginInt("plugin:hyprtrizone:indicator_height", 40);
    const auto indicatorFontSize    = getPluginInt("plugin:hyprtrizone:indicator_font_size", 24);
    const auto indicatorPadding     = getPluginInt("plugin:hyprtrizone:indicator_padding", 8);
    const auto indicatorOpacity     = getPluginFloat("plugin:hyprtrizone:indicator_opacity", 0.78F);
    const auto indicatorTextOpacity = getPluginFloat("plugin:hyprtrizone:indicator_text_opacity", 1.0F);
    const auto indicatorTextColor   = getPluginInt("plugin:hyprtrizone:indicator_text_color", 0xFFFFFFFF);
    const auto showZeroIndicators   = getPluginInt("plugin:hyprtrizone:indicator_show_zero", 0) != 0;

    // Convert work area to monitor-local coordinates for rendering
    const float WORKX = WORKAREA.x - PMONITOR->m_position.x;
    const float WORKY = WORKAREA.y - PMONITOR->m_position.y;
    const float WORKW = WORKAREA.w;
    const float WORKH = WORKAREA.h;

    const float masterWidth = WORKW * m_masterFactor;
    const float sideWidth   = (WORKW - masterWidth) / 2.0f;
    const float rightX      = WORKX + sideWidth + masterWidth;

    auto renderIndicator = [&](eHyprtrizoneZone zone) {
        const auto contentBoxGlobal = getZoneContentBox(zone);
        if (contentBoxGlobal.h <= 0 || contentBoxGlobal.w <= 0)
            return;

        const CBox contentBox = {contentBoxGlobal.x - PMONITOR->m_position.x, contentBoxGlobal.y - PMONITOR->m_position.y, contentBoxGlobal.w, contentBoxGlobal.h};
        const auto rows      = buildSideRows(zone);
        const int  sideCount = rows.size();
        if (sideCount <= 0)
            return;

        const auto metrics = getSideLayoutMetrics(zone, rows, contentBox.h);
        if (metrics.rowHeights.size() != rows.size())
            return;

        const float offset = std::clamp(getSideScrollOffset(zone), 0.f, std::max(0.f, metrics.contentHeight - (float)contentBox.h));
        int         hiddenAbove = 0;
        int         hiddenBelow = 0;

        for (int i = 0; i < (int)rows.size(); ++i) {
            const float rowTop    = metrics.rowTops[i] - offset;
            const float rowBottom = rowTop + metrics.rowHeights[i];
            const int   inRow     = rows[i].windowCount();

            if (rowBottom <= 0.f)
                hiddenAbove += inRow;
            else if (rowTop >= contentBox.h)
                hiddenBelow += inRow;
        }

        const float zoneX       = contentBox.x;
        const float badgeWidth  = std::clamp((float)indicatorWidth, 20.f, (float)std::max(20.0, contentBox.w - 4.0));
        const float badgeHeight = std::clamp((float)indicatorHeight, 14.f, (float)std::max(14.0, contentBox.h / 3.0));
        const float badgeX      = zoneX + (contentBox.w - badgeWidth) / 2.f;
        const float badgePad    = std::max(0.f, (float)indicatorPadding);
        const float badgeAlpha  = std::clamp((float)indicatorOpacity, 0.05f, 1.f);
        const float textAlpha   = std::clamp((float)indicatorTextOpacity, 0.0f, 1.f);
        const int   fontSize    = std::clamp((int)indicatorFontSize, 8, 72);
        CHyprColor  textColor   = CHyprColor(indicatorTextColor);
        textColor.a *= textAlpha;

        auto drawBadge = [&](float y, int value) {
            if (value <= 0 && !showZeroIndicators)
                return;

            CBox badge = {badgeX, y, badgeWidth, badgeHeight};
            badge.scale(PMONITOR->m_scale);
            badge.round();

            Render::GL::g_pHyprOpenGL->renderRect(ctx, badge, CHyprColor{0.08f, 0.08f, 0.08f, badgeAlpha}, {.damage = &FULL_REGION, .round = (int)(12 * PMONITOR->m_scale)});

            const auto textTex = g_pHyprRenderer->renderText(std::to_string(value), textColor, std::max(8, (int)(fontSize * PMONITOR->m_scale)), false, "", 0, 900);
            if (!textTex)
                return;

            CBox textBox = {badge.x + (badge.w - textTex->m_size.x) / 2.f, badge.y + (badge.h - textTex->m_size.y) / 2.f, textTex->m_size.x, textTex->m_size.y};
            textBox.round();
            Render::GL::g_pHyprOpenGL->renderTexture(ctx, textTex, textBox, {.damage = &FULL_REGION});
        };

        drawBadge(contentBox.y + badgePad, hiddenAbove);
        drawBadge(contentBox.y + contentBox.h - badgeHeight - badgePad, hiddenBelow);
    };

    renderIndicator(HYPRTRIZONE_ZONE_LEFT);
    renderIndicator(HYPRTRIZONE_ZONE_RIGHT);
}
