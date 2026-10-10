#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

class CStackIndicatorPassElement : public IPassElement {
  public:
    CStackIndicatorPassElement() = default;
    virtual ~CStackIndicatorPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx);
    virtual bool        needsLiveBlur(Render::CRenderContext& ctx);
    virtual bool        needsPrecomputeBlur(Render::CRenderContext& ctx);
    virtual ePassElementType type() {
        return EK_CUSTOM;
    }

    virtual const char* passName() {
        return "CStackIndicatorPassElement";
    }
};
