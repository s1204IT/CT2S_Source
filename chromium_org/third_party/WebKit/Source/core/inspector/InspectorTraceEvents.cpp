// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/inspector/InspectorTraceEvents.h"

#include "bindings/core/v8/ScriptCallStackFactory.h"
#include "bindings/core/v8/ScriptGCEvent.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "core/events/Event.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/inspector/IdentifiersFactory.h"
#include "core/inspector/InspectorNodeIds.h"
#include "core/inspector/ScriptCallStack.h"
#include "core/page/Page.h"
#include "core/rendering/RenderImage.h"
#include "core/rendering/RenderLayer.h"
#include "core/rendering/RenderObject.h"
#include "core/workers/WorkerThread.h"
#include "core/xml/XMLHttpRequest.h"
#include "platform/JSONValues.h"
#include "platform/TracedValue.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/network/ResourceRequest.h"
#include "platform/network/ResourceResponse.h"
#include "platform/weborigin/KURL.h"
#include "wtf/Vector.h"
#include <inttypes.h>

namespace blink {

static const unsigned maxInvalidationTrackingCallstackSize = 5;

namespace {

class JSCallStack : public TraceEvent::ConvertableToTraceFormat  {
public:
    explicit JSCallStack(PassRefPtrWillBeRawPtr<ScriptCallStack> callstack)
    {
        m_serialized = callstack ? callstack->buildInspectorArray()->toJSONString() : "[]";
        ASSERT(m_serialized.isSafeToSendToAnotherThread());
    }
    virtual String asTraceFormat() const
    {
        return m_serialized;
    }

private:
    String m_serialized;
};

String toHexString(const void* p)
{
    return String::format("0x%" PRIx64, static_cast<uint64>(reinterpret_cast<intptr_t>(p)));
}

}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorLayoutEvent::beginData(FrameView* frameView)
{
    bool isPartial;
    unsigned needsLayoutObjects;
    unsigned totalObjects;
    LocalFrame& frame = frameView->frame();
    frame.countObjectsNeedingLayout(needsLayoutObjects, totalObjects, isPartial);

    RefPtr<TracedValue> value = TracedValue::create();
    value->setInteger("dirtyObjects", needsLayoutObjects);
    value->setInteger("totalObjects", totalObjects);
    value->setBoolean("partialLayout", isPartial);
    value->setString("frame", toHexString(&frame));
    return value;
}

static void createQuad(TracedValue* value, const char* name, const FloatQuad& quad)
{
    value->beginArray(name);
    value->pushDouble(quad.p1().x());
    value->pushDouble(quad.p1().y());
    value->pushDouble(quad.p2().x());
    value->pushDouble(quad.p2().y());
    value->pushDouble(quad.p3().x());
    value->pushDouble(quad.p3().y());
    value->pushDouble(quad.p4().x());
    value->pushDouble(quad.p4().y());
    value->endArray();
}

static void setGeneratingNodeInfo(TracedValue* value, const RenderObject* renderer, const char* idFieldName, const char* nameFieldName = 0)
{
    Node* node = 0;
    for (; renderer && !node; renderer = renderer->parent())
        node = renderer->generatingNode();
    if (!node)
        return;
    value->setInteger(idFieldName, InspectorNodeIds::idForNode(node));
    if (nameFieldName)
        value->setString(nameFieldName, node->debugName());
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorLayoutEvent::endData(RenderObject* rootForThisLayout)
{
    Vector<FloatQuad> quads;
    rootForThisLayout->absoluteQuads(quads);

    RefPtr<TracedValue> value = TracedValue::create();
    if (quads.size() >= 1) {
        createQuad(value.get(), "root", quads[0]);
        setGeneratingNodeInfo(value.get(), rootForThisLayout, "rootNode");
    } else {
        ASSERT_NOT_REACHED();
    }
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorLayoutInvalidationTrackingEvent::data(const RenderObject* renderer)
{
    ASSERT(renderer);
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("frame", toHexString(renderer->frame()));
    setGeneratingNodeInfo(value.get(), renderer, "nodeId", "nodeName");
    RefPtrWillBeRawPtr<ScriptCallStack> callstack = createScriptCallStack(maxInvalidationTrackingCallstackSize, true);
    value->setString("callstack", callstack ? callstack->buildInspectorArray()->toJSONString() : "[]");
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorPaintInvalidationTrackingEvent::data(const RenderObject* renderer, const RenderObject* paintContainer)
{
    ASSERT(renderer);
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("frame", toHexString(renderer->frame()));
    setGeneratingNodeInfo(value.get(), paintContainer, "paintId");
    setGeneratingNodeInfo(value.get(), renderer, "nodeId", "nodeName");
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorSendRequestEvent::data(unsigned long identifier, LocalFrame* frame, const ResourceRequest& request)
{
    String requestId = IdentifiersFactory::requestId(identifier);

    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("requestId", requestId);
    value->setString("frame", toHexString(frame));
    value->setString("url", request.url().string());
    value->setString("requestMethod", request.httpMethod());
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorReceiveResponseEvent::data(unsigned long identifier, LocalFrame* frame, const ResourceResponse& response)
{
    String requestId = IdentifiersFactory::requestId(identifier);

    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("requestId", requestId);
    value->setString("frame", toHexString(frame));
    value->setInteger("statusCode", response.httpStatusCode());
    value->setString("mimeType", response.mimeType().string().isolatedCopy());
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorReceiveDataEvent::data(unsigned long identifier, LocalFrame* frame, int encodedDataLength)
{
    String requestId = IdentifiersFactory::requestId(identifier);

    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("requestId", requestId);
    value->setString("frame", toHexString(frame));
    value->setInteger("encodedDataLength", encodedDataLength);
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorResourceFinishEvent::data(unsigned long identifier, double finishTime, bool didFail)
{
    String requestId = IdentifiersFactory::requestId(identifier);

    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("requestId", requestId);
    value->setBoolean("didFail", didFail);
    if (finishTime)
        value->setDouble("networkTime", finishTime);
    return value;
}

static LocalFrame* frameForExecutionContext(ExecutionContext* context)
{
    LocalFrame* frame = 0;
    if (context->isDocument())
        frame = toDocument(context)->frame();
    return frame;
}

static PassRefPtr<TracedValue> genericTimerData(ExecutionContext* context, int timerId)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setInteger("timerId", timerId);
    if (LocalFrame* frame = frameForExecutionContext(context))
        value->setString("frame", toHexString(frame));
    return value.release();
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorTimerInstallEvent::data(ExecutionContext* context, int timerId, int timeout, bool singleShot)
{
    RefPtr<TracedValue> value = genericTimerData(context, timerId);
    value->setInteger("timeout", timeout);
    value->setBoolean("singleShot", singleShot);
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorTimerRemoveEvent::data(ExecutionContext* context, int timerId)
{
    return genericTimerData(context, timerId);
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorTimerFireEvent::data(ExecutionContext* context, int timerId)
{
    return genericTimerData(context, timerId);
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorAnimationFrameEvent::data(Document* document, int callbackId)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setInteger("id", callbackId);
    value->setString("frame", toHexString(document->frame()));
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorWebSocketCreateEvent::data(Document* document, unsigned long identifier, const KURL& url, const String& protocol)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setInteger("identifier", identifier);
    value->setString("url", url.string());
    value->setString("frame", toHexString(document->frame()));
    if (!protocol.isNull())
        value->setString("webSocketProtocol", protocol);
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorWebSocketEvent::data(Document* document, unsigned long identifier)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setInteger("identifier", identifier);
    value->setString("frame", toHexString(document->frame()));
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorParseHtmlEvent::beginData(Document* document, unsigned startLine)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setInteger("startLine", startLine);
    value->setString("frame", toHexString(document->frame()));
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorXhrReadyStateChangeEvent::data(ExecutionContext* context, XMLHttpRequest* request)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("url", request->url().string());
    value->setInteger("readyState", request->readyState());
    if (LocalFrame* frame = frameForExecutionContext(context))
        value->setString("frame", toHexString(frame));
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorXhrLoadEvent::data(ExecutionContext* context, XMLHttpRequest* request)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("url", request->url().string());
    if (LocalFrame* frame = frameForExecutionContext(context))
        value->setString("frame", toHexString(frame));
    return value;
}

static void localToPageQuad(const RenderObject& renderer, const LayoutRect& rect, FloatQuad* quad)
{
    LocalFrame* frame = renderer.frame();
    FrameView* view = frame->view();
    FloatQuad absolute = renderer.localToAbsoluteQuad(FloatQuad(rect));
    quad->setP1(view->contentsToRootView(roundedIntPoint(absolute.p1())));
    quad->setP2(view->contentsToRootView(roundedIntPoint(absolute.p2())));
    quad->setP3(view->contentsToRootView(roundedIntPoint(absolute.p3())));
    quad->setP4(view->contentsToRootView(roundedIntPoint(absolute.p4())));
}

const char InspectorLayerInvalidationTrackingEvent::SquashingLayerGeometryWasUpdated[] = "Squashing layer geometry was updated.";
const char InspectorLayerInvalidationTrackingEvent::AddedToSquashingLayer[] = "The layer may have been added to an already-existing squashing layer.";
const char InspectorLayerInvalidationTrackingEvent::RemovedFromSquashingLayer[] = "Removed the layer from a squashing layer.";
const char InspectorLayerInvalidationTrackingEvent::ReflectionLayerChanged[] = "Reflection layer change.";
const char InspectorLayerInvalidationTrackingEvent::NewCompositedLayer[] = "Assigned a new composited layer.";
const char InspectorLayerInvalidationTrackingEvent::AncestorRequiresNewLayer[] = "A new composited layer is needed based on the RenderLayer's compositing ancestor's properties.";

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorLayerInvalidationTrackingEvent::data(const RenderLayer* layer, const char* reason)
{
    const RenderObject* paintInvalidationContainer = layer->renderer()->containerForPaintInvalidation();

    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("frame", toHexString(paintInvalidationContainer->frame()));
    setGeneratingNodeInfo(value.get(), paintInvalidationContainer, "paintId");
    value->setString("reason", reason);
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorPaintEvent::data(RenderObject* renderer, const LayoutRect& clipRect, const GraphicsLayer* graphicsLayer)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("frame", toHexString(renderer->frame()));
    FloatQuad quad;
    localToPageQuad(*renderer, clipRect, &quad);
    createQuad(value.get(), "clip", quad);
    setGeneratingNodeInfo(value.get(), renderer, "nodeId");
    int graphicsLayerId = graphicsLayer ? graphicsLayer->platformLayer()->id() : 0;
    value->setInteger("layerId", graphicsLayerId);
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorMarkLoadEvent::data(LocalFrame* frame)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("frame", toHexString(frame));
    bool isMainFrame = frame && frame->isMainFrame();
    value->setBoolean("isMainFrame", isMainFrame);
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorScrollLayerEvent::data(RenderObject* renderer)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("frame", toHexString(renderer->frame()));
    setGeneratingNodeInfo(value.get(), renderer, "nodeId");
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorEvaluateScriptEvent::data(LocalFrame* frame, const String& url, int lineNumber)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("frame", toHexString(frame));
    value->setString("url", url);
    value->setInteger("lineNumber", lineNumber);
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorFunctionCallEvent::data(ExecutionContext* context, int scriptId, const String& scriptName, int scriptLine)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("scriptId", String::number(scriptId));
    value->setString("scriptName", scriptName);
    value->setInteger("scriptLine", scriptLine);
    if (LocalFrame* frame = frameForExecutionContext(context))
        value->setString("frame", toHexString(frame));
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorPaintImageEvent::data(const RenderImage& renderImage)
{
    RefPtr<TracedValue> value = TracedValue::create();
    setGeneratingNodeInfo(value.get(), &renderImage, "nodeId");
    if (const ImageResource* resource = renderImage.cachedImage())
        value->setString("url", resource->url().string());
    return value;
}

static size_t usedHeapSize()
{
    HeapInfo info;
    ScriptGCEvent::getHeapSize(info);
    return info.usedJSHeapSize;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorUpdateCountersEvent::data()
{
    RefPtr<TracedValue> value = TracedValue::create();
    if (isMainThread()) {
        value->setInteger("documents", InspectorCounters::counterValue(InspectorCounters::DocumentCounter));
        value->setInteger("nodes", InspectorCounters::counterValue(InspectorCounters::NodeCounter));
        value->setInteger("jsEventListeners", InspectorCounters::counterValue(InspectorCounters::JSEventListenerCounter));
    }
    value->setDouble("jsHeapSizeUsed", static_cast<double>(usedHeapSize()));
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorCallStackEvent::currentCallStack()
{
    return adoptRef(new JSCallStack(createScriptCallStack(ScriptCallStack::maxCallStackSizeToCapture, true)));
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorEventDispatchEvent::data(const Event& event)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("type", event.type());
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorTimeStampEvent::data(ExecutionContext* context, const String& message)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("message", message);
    if (LocalFrame* frame = frameForExecutionContext(context))
        value->setString("frame", toHexString(frame));
    return value;
}

PassRefPtr<TraceEvent::ConvertableToTraceFormat> InspectorTracingSessionIdForWorkerEvent::data(const String& sessionId, WorkerThread* workerThread)
{
    RefPtr<TracedValue> value = TracedValue::create();
    value->setString("sessionId", sessionId);
    value->setDouble("workerThreadId", workerThread->platformThreadId());
    return value;
}

}
