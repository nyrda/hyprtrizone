#pragma once

#include "globals.hpp"
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/helpers/math/Direction.hpp>
#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <chrono>
#include <list>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

enum eHyprtrizoneZone : uint8_t {
    HYPRTRIZONE_ZONE_CENTER = 0,
    HYPRTRIZONE_ZONE_LEFT   = 1,
    HYPRTRIZONE_ZONE_RIGHT  = 2,
};

// Global state shared across all per-workspace algorithm instances
struct CHyprtrizoneGlobals {
    static std::optional<bool> scrollingEnabledOverride;
    static std::optional<bool> pairingLeftOverride;
    static std::optional<bool> pairingRightOverride;

    static bool isScrollingEnabled();
};

class CHyprtrizoneAlgorithm : public Layout::ITiledAlgorithm {
  public:
    CHyprtrizoneAlgorithm();
    virtual ~CHyprtrizoneAlgorithm();

    // IModeAlgorithm pure virtuals
    virtual void newTarget(SP<Layout::ITarget> target) override;
    virtual void movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    virtual void removeTarget(SP<Layout::ITarget> target) override;
    virtual void resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    virtual void recalculate(Layout::eRecalculateReason reason = Layout::RECALCULATE_REASON_UNKNOWN) override;
    virtual void swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    virtual void moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection dir, bool silent) override;

    // ITiledAlgorithm pure virtual
    virtual SP<Layout::ITarget> getNextCandidate(SP<Layout::ITarget> old) override;

    // Optional overrides
    virtual Config::ErrorResult              layoutMsg(const std::string_view& sv) override;
    virtual std::optional<Vector2D>          predictSizeForNewTarget() override;

    // For indicator rendering (called from render callback)
    void renderStackIndicators(PHLMONITOR pMonitor, Render::CRenderContext& ctx);

    // Global registry for render callback access
    static std::set<CHyprtrizoneAlgorithm*>& instances();
    static CHyprtrizoneAlgorithm*            fromWindow(PHLWINDOW pWindow);
    static CHyprtrizoneAlgorithm*            activeOnMonitor(PHLMONITOR pMonitor);
    static void                              pruneCachedWindowState();

    // Scrolls a side stack for a mouse wheel event while wheel_scroll_mods are held. Returns whether the event was consumed.
    static bool handleWheelEvent(const IPointer::SAxisEvent& event);

    // Public workspace accessor for render callback
    PHLWORKSPACE workspace() const;
    bool         shouldWindowFollowWorkspaces(PHLWINDOW pWindow) const;
    bool         prepareWindowForWorkspaceFollow(PHLWINDOW pWindow) const;
    CBox         getZoneBox(eHyprtrizoneZone zone) const;
    CBox         getZoneContentBox(eHyprtrizoneZone zone) const;
    int          getZoneHeaderHeight(eHyprtrizoneZone zone) const;

  private:
    struct SNodeData {
        eHyprtrizoneZone zone     = HYPRTRIZONE_ZONE_CENTER;
        float                  percSize = 1.f;
        WP<Layout::ITarget>    target;
        Vector2D               position;
        Vector2D               size;
        bool                   pairWithPrevious = false;
        // Position in the layout-switch snapshot this node was restored from (0: not restored from one)
        uint64_t               restoreBatch = 0;
        int                    restoreOrder = 0;

        bool operator==(const SNodeData& rhs) const {
            return target.lock() == rhs.target.lock();
        }
    };

    struct SSideRow {
        SNodeData* first  = nullptr;
        SNodeData* second = nullptr;

        int windowCount() const {
            return (first ? 1 : 0) + (second ? 1 : 0);
        }
    };

    struct SSideLayoutMetrics {
        float              slotHeight    = 0.f;
        float              contentHeight = 0.f;
        std::vector<float> rowTops;
        std::vector<float> rowHeights;
    };

    // Consecutive wheel ticks in one direction that arrived while the stack was still moving
    struct SWheelStreak {
        float                                 direction = 0.f;
        float                                 notches   = 0.f;
        float                                 offset    = 0.f;
        std::chrono::steady_clock::time_point lastTick;
    };

    std::list<SNodeData> m_nodes;

    // Per-workspace layout state
    float m_masterFactor      = 0.5f;
    bool  m_masterResized     = false;
    float m_leftScrollOffset  = 0.f;
    float m_rightScrollOffset = 0.f;
    SWheelStreak m_leftWheelStreak;
    SWheelStreak m_rightWheelStreak;
    bool  m_forceWarps        = false;
    // Set once Hyprland starts handing every target to another layout; this instance is then discarded
    bool  m_layoutTeardown    = false;
    // removeTarget called by this layout itself (dropon); the target is still in Hyprland's list then
    bool  m_internalRemoval   = false;
    // Saved during removeTarget for getNextCandidate (node is already gone when it's called)
    WP<Layout::ITarget>    m_lastRemovedPairPartner;
    WP<Layout::ITarget>    m_lastRemovedNextInZone;
    WP<Layout::ITarget>    m_lastRemovedPrevInZone;

    // Workspace / space access helpers
    PHLMONITOR getMonitor() const;
    CBox       getWorkArea() const;

    // Node helpers
    SNodeData*                getNodeFromTarget(SP<Layout::ITarget> target);
    SNodeData*                getNodeFromWindow(PHLWINDOW pWindow);
    SNodeData*                getMasterNode();
    int                       getNodesInZone(eHyprtrizoneZone zone) const;
    int                       getNodeCount() const;

    // Zone helpers
    eHyprtrizoneZone getActiveZone() const;
    eHyprtrizoneZone getZoneAtCursor() const;
    eHyprtrizoneZone getBiasZone() const;

    // Scrolling helpers
    float getSideScrollOffset(eHyprtrizoneZone zone) const;
    void  setSideScrollOffset(eHyprtrizoneZone zone, float offset);
    int   getSideVisibleWindowCount(eHyprtrizoneZone zone) const;
    float getSideSlotHeight(eHyprtrizoneZone zone, float workspaceHeight, int sideCount) const;
    float getSideMaxScroll(eHyprtrizoneZone zone, float workspaceHeight, int sideCount) const;
    void  clampScrollOffsets();
    void  snapZoneToEnd(eHyprtrizoneZone zone);
    bool  scrollZoneByRows(eHyprtrizoneZone zone, float rows, float amountMult = 1.f);
    void  scrollZone(eHyprtrizoneZone zone, float direction, float amountMult = 1.f);
    void  wheelScrollZone(eHyprtrizoneZone zone, float direction, float notches);
    bool  isZoneMoving(eHyprtrizoneZone zone) const;
    void  focusWindowInSideStack(SP<Layout::ITarget> target);

    // Zone preference helpers
    std::optional<eHyprtrizoneZone> getPreferredZoneForWindow(PHLWINDOW pWindow) const;
    void                                  insertNodeAtTopOfZone(std::list<SNodeData>::iterator nodeIt, eHyprtrizoneZone zone);
    void                                  displaceUntaggedCenterWindows();

    // Pairing helpers
    bool                  isPairingEnabledForZone(eHyprtrizoneZone zone) const;
    bool                  isWindowPairEligible(PHLWINDOW pWindow, eHyprtrizoneZone zone) const;
    int                   getSideRowCount(eHyprtrizoneZone zone) const;
    std::vector<SSideRow> buildSideRows(eHyprtrizoneZone zone) const;
    SNodeData*            getSideRowLeader(SNodeData* pNode);
    std::list<SNodeData>::iterator findNodeIt(SNodeData* pNode);
    std::list<SNodeData>::iterator prevNodeInZone(std::list<SNodeData>::iterator it);
    std::list<SNodeData>::iterator nextNodeInZone(std::list<SNodeData>::iterator it);
    SNodeData*                     getPairPartner(SNodeData* pNode);
    void                           detachFromPair(SNodeData* pNode);
    void                           changeNodeZone(SNodeData* pNode, eHyprtrizoneZone zone);
    bool                           placeTargetOnWindow(SP<Layout::ITarget> target, PHLWINDOW pWindowUnder, const Vector2D& dropPos);
    std::list<SNodeData>::iterator getInsertPositionAfterSideRow(std::list<SNodeData>::iterator anchorIt);
    bool                           shouldPairWithPrevious(std::list<SNodeData>::iterator nodeIt) const;
    void                           moveNodeToZoneOrder(std::list<SNodeData>::iterator nodeIt, eHyprtrizoneZone zone, int orderInZone);
    SSideLayoutMetrics    getSideLayoutMetrics(eHyprtrizoneZone zone, const std::vector<SSideRow>& rows, float workspaceHeight) const;

    // Window positioning
    void applyNodeDataToWindow(SNodeData* pNode);

    // Focused target lookup for layoutMsg
    SP<Layout::ITarget> getFocusedTarget();
};
