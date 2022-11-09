/*
 * Copyright (C) 2004, 2005, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2007 Rob Buis <buis@kde.org>
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

#ifndef SVGScriptElement_h
#define SVGScriptElement_h

#include "core/SVGNames.h"
#include "core/dom/ScriptLoaderClient.h"
#include "core/svg/SVGAnimatedBoolean.h"
#include "core/svg/SVGAnimatedString.h"
#include "core/svg/SVGElement.h"
#include "core/svg/SVGURIReference.h"

namespace blink {

class ScriptLoader;

class SVGScriptElement FINAL
    : public SVGElement
    , public SVGURIReference
    , public ScriptLoaderClient {
    DEFINE_WRAPPERTYPEINFO();
public:
    static PassRefPtrWillBeRawPtr<SVGScriptElement> create(Document&, bool wasInsertedByParser);

    ScriptLoader* loader() const { return m_loader.get(); }

#if ENABLE(ASSERT)
    virtual bool isAnimatableAttribute(const QualifiedName&) const OVERRIDE;
#endif

private:
    SVGScriptElement(Document&, bool wasInsertedByParser, bool alreadyStarted);
    virtual ~SVGScriptElement();

    bool isSupportedAttribute(const QualifiedName&);
    virtual void parseAttribute(const QualifiedName&, const AtomicString&) OVERRIDE;
    virtual InsertionNotificationRequest insertedInto(ContainerNode*) OVERRIDE;
    virtual void didNotifySubtreeInsertionsToDocument() OVERRIDE;
    virtual void childrenChanged(const ChildrenChange&) OVERRIDE;
    virtual void didMoveToNewDocument(Document& oldDocument) OVERRIDE;

    virtual void svgAttributeChanged(const QualifiedName&) OVERRIDE;
    virtual bool isURLAttribute(const Attribute&) const OVERRIDE;
    virtual bool isStructurallyExternal() const OVERRIDE { return hasSourceAttribute(); }
    virtual void finishParsingChildren() OVERRIDE;

    virtual bool haveLoadedRequiredResources() OVERRIDE;

    virtual String sourceAttributeValue() const OVERRIDE;
    virtual String charsetAttributeValue() const OVERRIDE;
    virtual String typeAttributeValue() const OVERRIDE;
    virtual String languageAttributeValue() const OVERRIDE;
    virtual String forAttributeValue() const OVERRIDE;
    virtual String eventAttributeValue() const OVERRIDE;
    virtual bool asyncAttributeValue() const OVERRIDE;
    virtual bool deferAttributeValue() const OVERRIDE;
    virtual bool hasSourceAttribute() const OVERRIDE;

    virtual void dispatchLoadEvent() OVERRIDE;

    virtual PassRefPtrWillBeRawPtr<Element> cloneElementWithoutAttributesAndChildren() OVERRIDE;
    virtual bool rendererIsNeeded(const RenderStyle&) OVERRIDE { return false; }

    virtual Timer<SVGElement>* svgLoadEventTimer() OVERRIDE { return &m_svgLoadEventTimer; }


    Timer<SVGElement> m_svgLoadEventTimer;
    OwnPtr<ScriptLoader> m_loader;
};

} // namespace blink

#endif // SVGScriptElement_h
