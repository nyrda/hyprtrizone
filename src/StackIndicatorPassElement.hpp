#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

class CStackIndicatorPassElement : public IPassElement {
  public:
    CStackIndicatorPassElement() = default;
    virtual ~CStackIndicatorPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw();
    virtual bool        needsLiveBlur();
    virtual bool        needsPrecomputeBlur();
    virtual ePassElementType type() {
        return EK_CUSTOM;
    }

    virtual const char* passName() {
        return "CStackIndicatorPassElement";
    }
};
