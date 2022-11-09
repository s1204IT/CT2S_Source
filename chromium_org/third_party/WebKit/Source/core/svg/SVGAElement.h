/*
 * Copyright (C) 2004, 2005, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2007 Eric Seidel <eric@webkit.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef SVGAElement_h
#define SVGAElement_h

#include "core/svg/SVGAnimatedBoolean.h"
#include "core/svg/SVGGraphicsElement.h"
#include "core/svg/SVGURIReference.h"

namespace blink {

class SVGAElement FINAL : public SVGGraphicsElement,
                          public SVGURIReference {
    DEFINE_WRAPPERTYPEINFO();
public:
    DECLARE_NODE_FACTORY(SVGAElement);
    SVGAnimatedString* svgTarget() { return m_svgTarget.get(); }

private:
    explicit SVGAElement(Document&);

    virtual String title() const OVERRIDE;

    virtual void parseAttribute(const QualifiedName&, const AtomicString&) OVERRIDE;
    virtual void svgAttributeChanged(const QualifiedName&) OVERRIDE;

    virtual RenderObject* createRenderer(RenderStyle*) OVERRIDE;

    virtual void defaultEventHandler(Event*) OVERRIDE;

    virtual bool isLiveLink() const OVERRIDE { return isLink(); }

    virtual bool supportsFocus() const OVERRIDE;
    virtual bool shouldHaveFocusAppearance() const OVERRIDE FINAL;
    virtual void dispatchFocusEvent(Element* oldFocusedElement, FocusType) OVERRIDE;
    virtual bool isMouseFocusable() const OVERRIDE;
    virtual bool isKeyboardFocusable() const OVERRIDE;
    virtual bool isURLAttribute(const Attribute&) const OVERRIDE;
    virtual bool canStartSelection() const OVERRIDE;
    virtual short tabIndex() const OVERRIDE;

    virtual bool willRespondToMouseClickEvents() OVERRIDE;

    RefPtr<SVGAnimatedString> m_svgTarget;
    bool m_wasFocusedByMouse;
};

} // namespace blink

#endif // SVGAElement_h
