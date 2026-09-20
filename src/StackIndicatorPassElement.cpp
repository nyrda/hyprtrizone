#include "StackIndicatorPassElement.hpp"
#include "hyprtrizoneLayout.hpp"
#include <hyprland/src/render/Renderer.hpp>

std::vector<UP<IPassElement>> CStackIndicatorPassElement::draw() {
    auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!monitor)
        return {};

    for (auto* algo : CHyprtrizoneAlgorithm::instances()) {
        auto ws = algo->workspace();
        if (!ws)
            continue;
        if (ws == monitor->m_activeWorkspace || ws == monitor->m_activeSpecialWorkspace)
            algo->renderStackIndicators(monitor);
    }

    return {};
}

bool CStackIndicatorPassElement::needsLiveBlur() {
    return false;
}

bool CStackIndicatorPassElement::needsPrecomputeBlur() {
    return false;
}
