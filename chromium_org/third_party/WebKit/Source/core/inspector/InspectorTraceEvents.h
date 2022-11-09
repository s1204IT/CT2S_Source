// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef InspectorTraceEvents_h
#define InspectorTraceEvents_h

#include "platform/EventTracer.h"
#include "platform/TraceEvent.h"
#include "wtf/Forward.h"

namespace blink {

class Document;
class Event;
class ExecutionContext;
class FrameView;
class GraphicsContext;
class GraphicsLayer;
class KURL;
class LayoutRect;
class LocalFrame;
class RenderImage;
class RenderLayer;
class RenderObject;
class ResourceRequest;
class ResourceResponse;
class ScriptSourceCode;
class ScriptCallStack;
class WorkerThread;
class XMLHttpRequest;

class InspectorLayoutEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> beginData(FrameView*);
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> endData(RenderObject* rootForThisLayout);
};

class InspectorLayoutInvalidationTrackingEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(const RenderObject*);
};

class InspectorPaintInvalidationTrackingEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(const RenderObject* renderer, const RenderObject* paintContainer);
};

class InspectorSendRequestEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(unsigned long identifier, LocalFrame*, const ResourceRequest&);
};

class InspectorReceiveResponseEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(unsigned long identifier, LocalFrame*, const ResourceResponse&);
};

class InspectorReceiveDataEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(unsigned long identifier, LocalFrame*, int encodedDataLength);
};

class InspectorResourceFinishEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(unsigned long identifier, double finishTime, bool didFail);
};

class InspectorTimerInstallEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(ExecutionContext*, int timerId, int timeout, bool singleShot);
};

class InspectorTimerRemoveEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(ExecutionContext*, int timerId);
};

class InspectorTimerFireEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(ExecutionContext*, int timerId);
};

class InspectorAnimationFrameEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(Document*, int callbackId);
};

class InspectorWebSocketCreateEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(Document*, unsigned long identifier, const KURL&, const String& protocol);
};

class InspectorWebSocketEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(Document*, unsigned long identifier);
};

class InspectorParseHtmlEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> beginData(Document*, unsigned startLine);
};

class InspectorXhrReadyStateChangeEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(ExecutionContext*, XMLHttpRequest*);
};

class InspectorXhrLoadEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(ExecutionContext*, XMLHttpRequest*);
};

class InspectorLayerInvalidationTrackingEvent {
public:
    static const char SquashingLayerGeometryWasUpdated[];
    static const char AddedToSquashingLayer[];
    static const char RemovedFromSquashingLayer[];
    static const char ReflectionLayerChanged[];
    static const char NewCompositedLayer[];
    static const char AncestorRequiresNewLayer[];

    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(const RenderLayer*, const char* reason);
};
#define TRACE_LAYER_INVALIDATION(LAYER, REASON) \
    TRACE_EVENT_INSTANT1( \
        TRACE_DISABLED_BY_DEFAULT("devtools.timeline.invalidationTracking"), \
        "LayerInvalidationTracking", \
        "data", \
        InspectorLayerInvalidationTrackingEvent::data((LAYER), (REASON)))

class InspectorPaintEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(RenderObject*, const LayoutRect& clipRect, const GraphicsLayer*);
};

class InspectorPaintImageEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(const RenderImage&);
};

class InspectorMarkLoadEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(LocalFrame*);
};

class InspectorScrollLayerEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(RenderObject*);
};

class InspectorEvaluateScriptEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(LocalFrame*, const String& url, int lineNumber);
};

class InspectorFunctionCallEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(ExecutionContext*, int scriptId, const String& scriptName, int scriptLine);
};

class InspectorUpdateCountersEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data();
};

class InspectorCallStackEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> currentCallStack();
};

class InspectorEventDispatchEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(const Event&);
};

class InspectorTimeStampEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(ExecutionContext*, const String& message);
};

class InspectorTracingSessionIdForWorkerEvent {
public:
    static PassRefPtr<TraceEvent::ConvertableToTraceFormat> data(const String& sessionId, WorkerThread*);
};

} // namespace blink


#endif // !defined(InspectorTraceEvents_h)
