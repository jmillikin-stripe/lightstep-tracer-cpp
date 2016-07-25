// -*- Mode: C++ -*-

#include "options.h"
#include "propagation.h"
#include "span.h"
#include "types.h"
#include "value.h"

#include <memory>

#ifndef __LIGHTSTEP_TRACER_H__
#define __LIGHTSTEP_TRACER_H__

namespace json11 {
class Json;
}

namespace lightstep_net {
class KeyValue;
class TraceJoinId;
}

namespace lightstep {

// Tracer is a handle to a TracerImpl or acts as a No-Op.
class Tracer {
 public:
  explicit Tracer(const ImplPtr &impl) : impl_(impl) { }

  // Constructs a No-Op tracer handle. implementation.
  explicit Tracer(std::nullptr_t) { }

  Span StartSpan(const std::string& operation_name,
		 std::initializer_list<StartSpanOption> opts = {}) const;

  // GlobalTracer returns the global tracer.
  static Tracer Global();

  // InitGlobalTracer sets the global tracer pointer, returns the
  // former global tracer value.
  static Tracer InitGlobal(Tracer);
  
  // Get the implementation object.
  ImplPtr impl() const { return impl_; }

  // Inject() takes the `sc` SpanContext instance and injects it for
  // propagation within `carrier`. The actual type of `carrier` depends on
  // the value of `format`.
  //
  // OpenTracing defines a common set of `format` values (see BuiltinFormat),
  // and each has an expected carrier type.
  //
  // TODO throws?
  void Inject(SpanContext sc, const CarrierFormat &format, CarrierWriter *writer);

  // Extract() returns a SpanContext instance given `format` and `carrier`.
  //
  // OpenTracing defines a common set of `format` values (see BuiltinFormat),
  // and each has an expected carrier type.
  //
  // TODO throws?
  SpanContext Extract(const CarrierFormat &format, CarrierReader *reader);

 private:
  ImplPtr impl_;
};

// Recorder is an abstract class for buffering and encoding LightStep
// reports.
class Recorder {
public:
  virtual ~Recorder() { }

  // RecordSpan is called by TracerImpl when a new Span is finished.
  // An rvalue-reference is taken to avoid copying the Span contents.
  virtual void RecordSpan(lightstep_net::SpanRecord&& span) = 0;

  // Flush is called by the user, indicating for some reason that
  // buffered spans should be flushed.  Returns true if the flush
  // succeeded, false if it timed out.
  virtual bool FlushWithTimeout(Duration timeout) = 0;

  // Flush with an indefinite timeout.
  virtual void Flush() {
    // N.B.: Do not use Duration::max() it will overflow the internals
    // of std::condition_variable::wait_for, for example.
    FlushWithTimeout(std::chrono::hours(24));
    // TODO and panic when it fails? use absolute time instead, to
    // avoid the overflow issue?
  }
};

// JsonEncoder is used as the base class for the DefaultRecorder.
class JsonEncoder {
public:
  explicit JsonEncoder(const TracerImpl& tracer);

  void recordSpan(lightstep_net::SpanRecord&& span);

  size_t pendingSize() const {
    if (assembled_ < 0) {
      return 0;
    }
    return assembly_.size() + 3;  // 3 == strlen("] }")
  }

  // the caller can swap() the value, otherwise it stays valid until
  // the next call to recordSpan().
  std::string& jsonString();

private:
  json11::Json keyValueArray(const std::vector<lightstep_net::KeyValue>& v);
  json11::Json keyValueArray(const std::unordered_map<std::string, std::string>& v);
  json11::Json traceJoinArray(const std::vector<lightstep_net::TraceJoinId>& v);
  void setJsonPrefix();
  void addReportFields();
  void addJsonSuffix();

  const TracerImpl& tracer_;
 
  std::mutex mutex_;
  std::string assembly_;
  int assembled_;
};

enum SpanReferenceType {
  // ChildOfRef refers to a parent Span that caused *and* somehow depends
  // upon the new child Span. Often (but not always), the parent Span cannot
  // finish unitl the child Span does.
  //
  // An timing diagram for a ChildOfRef that's blocked on the new Span:
  //
  //     [-Parent Span---------]
  //          [-Child Span----]
  //
  // See http://opentracing.io/spec/
  //
  // See opentracing.ChildOf()
  ChildOfRef = 1,

  // FollowsFromRef refers to a parent Span that does not depend in any way
  // on the result of the new child Span. For instance, one might use
  // FollowsFromRefs to describe pipeline stages separated by queues,
  // or a fire-and-forget cache insert at the tail end of a web request.
  //
  // A FollowsFromRef Span is part of the same logical trace as the new Span:
  // i.e., the new Span is somehow caused by the work of its FollowsFromRef.
  //
  // All of the following could be valid timing diagrams for children that
  // "FollowFrom" a parent.
  //
  //     [-Parent Span-]  [-Child Span-]
  //
  //
  //     [-Parent Span--]
  //      [-Child Span-]
  //
  //
  //     [-Parent Span-]
  //                 [-Child Span-]
  //
  // See http://opentracing.io/spec/
  //
  // See opentracing.FollowsFrom()
  FollowsFromRef = 2
};

class SpanReference : public StartSpanOption {
public:
  SpanReference(SpanReferenceType type, const SpanContext &referenced)
    : type_(type),
      referenced_(referenced) { }

  virtual void Apply(SpanImpl *span) const;

private:
  SpanReferenceType type_;
  SpanContext referenced_;
};

// Create a ChildOf-referencing StartSpan option.
SpanReference ChildOf(const SpanContext& ctx);

// Create a FollowsFrom-referencing StartSpan option.
SpanReference FollowsFrom(const SpanContext& ctx);

}  // namespace lightstep

#endif // __LIGHTSTEP_TRACER_H__
