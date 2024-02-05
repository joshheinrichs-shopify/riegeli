// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RIEGELI_BYTES_POSITION_SHIFTING_READER_H_
#define RIEGELI_BYTES_POSITION_SHIFTING_READER_H_

#include <stddef.h>

#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/types/optional.h"
#include "riegeli/base/assert.h"
#include "riegeli/base/chain.h"
#include "riegeli/base/dependency.h"
#include "riegeli/base/object.h"
#include "riegeli/base/types.h"
#include "riegeli/bytes/reader.h"

namespace riegeli {

class BackwardWriter;
class Writer;

// Template parameter independent part of `PositionShiftingReader`.
class PositionShiftingReaderBase : public Reader {
 public:
  class Options {
   public:
    Options() noexcept {}

    // The base position of the new `Reader`.
    //
    // Default: 0.
    Options& set_base_pos(Position base_pos) & {
      base_pos_ = base_pos;
      return *this;
    }
    Options&& set_base_pos(Position base_pos) && {
      return std::move(set_base_pos(base_pos));
    }
    Position base_pos() const { return base_pos_; }

   private:
    Position base_pos_ = 0;
  };

  // Returns the original `Reader`. Unchanged by `Close()`.
  virtual Reader* SrcReader() = 0;
  virtual const Reader* SrcReader() const = 0;

  // Returns the base position of the new `Reader`.
  Position base_pos() const { return base_pos_; }

  bool ToleratesReadingAhead() override;
  bool SupportsRandomAccess() override;
  bool SupportsRewind() override;
  bool SupportsSize() override;
  bool SupportsNewReader() override;

 protected:
  explicit PositionShiftingReaderBase(Closed) noexcept : Reader(kClosed) {}

  explicit PositionShiftingReaderBase(Position base_pos);

  PositionShiftingReaderBase(PositionShiftingReaderBase&& that) noexcept;
  PositionShiftingReaderBase& operator=(
      PositionShiftingReaderBase&& that) noexcept;

  void Reset(Closed);
  void Reset(Position base_pos);
  void Initialize(Reader* src);
  ABSL_ATTRIBUTE_COLD absl::Status AnnotateOverSrc(absl::Status status);

  // Sets cursor of `src` to cursor of `*this`.
  void SyncBuffer(Reader& src);

  // Sets buffer pointers of `*this` to buffer pointers of `src`, adjusting
  // `start()` to hide data already read. Fails `*this` if `src` failed.
  void MakeBuffer(Reader& src);

  void Done() override;
  ABSL_ATTRIBUTE_COLD absl::Status AnnotateStatusImpl(
      absl::Status status) override;
  bool PullSlow(size_t min_length, size_t recommended_length) override;
  using Reader::ReadSlow;
  bool ReadSlow(size_t length, char* dest) override;
  bool ReadSlow(size_t length, Chain& dest) override;
  bool ReadSlow(size_t length, absl::Cord& dest) override;
  using Reader::CopySlow;
  bool CopySlow(Position length, Writer& dest) override;
  bool CopySlow(size_t length, BackwardWriter& dest) override;
  using Reader::ReadOrPullSomeSlow;
  bool ReadOrPullSomeSlow(size_t max_length,
                          absl::FunctionRef<char*(size_t&)> get_dest) override;
  void ReadHintSlow(size_t min_length, size_t recommended_length) override;
  bool SeekSlow(Position new_pos) override;
  absl::optional<Position> SizeImpl() override;
  std::unique_ptr<Reader> NewReaderImpl(Position initial_pos) override;

 private:
  ABSL_ATTRIBUTE_COLD bool FailUnderflow(Position new_pos, Object& object);

  // This template is defined and used only in position_shifting_reader.cc.
  template <typename Dest>
  bool ReadInternal(size_t length, Dest& dest);

  Position base_pos_ = 0;

  // Invariants if `is_open()`:
  //   `start() >= SrcReader()->cursor()`
  //   `limit() == SrcReader()->limit()`
  //   `limit_pos() == SrcReader()->limit_pos() + base_pos_`
};

// A `Reader` which reads from another `Reader`, reporting positions shifted so
// that the beginning appears as the given base position. Seeking back before
// the base position fails.
//
// `PrefixLimitingReader` can be used for shifting positions in the other
// direction.
//
// The `Src` template parameter specifies the type of the object providing and
// possibly owning the original `Reader`. `Src` must support
// `Dependency<Reader*, Src>`, e.g. `Reader*` (not owned, default),
// `ChainReader<>` (owned), `std::unique_ptr<Reader>` (owned),
// `AnyDependency<Reader*>` (maybe owned).
//
// By relying on CTAD the template argument can be deduced as the value type of
// the first constructor argument. This requires C++17.
//
// The original `Reader` must not be accessed until the `PositionShiftingReader`
// is closed or no longer used.
template <typename Src = Reader*>
class PositionShiftingReader : public PositionShiftingReaderBase {
 public:
  // Creates a closed `PositionShiftingReader`.
  explicit PositionShiftingReader(Closed) noexcept
      : PositionShiftingReaderBase(kClosed) {}

  // Will read from the original `Reader` provided by `src`.
  explicit PositionShiftingReader(const Src& src, Options options = Options());
  explicit PositionShiftingReader(Src&& src, Options options = Options());

  // Will read from the original `Reader` provided by a `Src` constructed from
  // elements of `src_args`. This avoids constructing a temporary `Src` and
  // moving from it.
  template <typename... SrcArgs>
  explicit PositionShiftingReader(std::tuple<SrcArgs...> src_args,
                                  Options options = Options());

  PositionShiftingReader(PositionShiftingReader&& that) noexcept;
  PositionShiftingReader& operator=(PositionShiftingReader&& that) noexcept;

  // Makes `*this` equivalent to a newly constructed `PositionShiftingReader`.
  // This avoids constructing a temporary `PositionShiftingReader` and moving
  // from it.
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(Closed);
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(const Src& src,
                                          Options options = Options());
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(Src&& src,
                                          Options options = Options());
  template <typename... SrcArgs>
  ABSL_ATTRIBUTE_REINITIALIZES void Reset(std::tuple<SrcArgs...> src_args,
                                          Options options = Options());

  // Returns the object providing and possibly owning the original `Reader`.
  // Unchanged by `Close()`.
  Src& src() { return src_.manager(); }
  const Src& src() const { return src_.manager(); }
  Reader* SrcReader() override { return src_.get(); }
  const Reader* SrcReader() const override { return src_.get(); }

 protected:
  void Done() override;
  void SetReadAllHintImpl(bool read_all_hint) override;
  void VerifyEndImpl() override;
  bool SyncImpl(SyncType sync_type) override;

 private:
  // Moves `that.src_` to `src_`. Buffer pointers are already moved from `src_`
  // to `*this`; adjust them to match `src_`.
  void MoveSrc(PositionShiftingReader&& that);

  // The object providing and possibly owning the original `Reader`.
  Dependency<Reader*, Src> src_;
};

// Support CTAD.
#if __cpp_deduction_guides
explicit PositionShiftingReader(Closed)
    -> PositionShiftingReader<DeleteCtad<Closed>>;
template <typename Src>
explicit PositionShiftingReader(const Src& src,
                                PositionShiftingReaderBase::Options options =
                                    PositionShiftingReaderBase::Options())
    -> PositionShiftingReader<std::decay_t<Src>>;
template <typename Src>
explicit PositionShiftingReader(Src&& src,
                                PositionShiftingReaderBase::Options options =
                                    PositionShiftingReaderBase::Options())
    -> PositionShiftingReader<std::decay_t<Src>>;
template <typename... SrcArgs>
explicit PositionShiftingReader(std::tuple<SrcArgs...> src_args,
                                PositionShiftingReaderBase::Options options =
                                    PositionShiftingReaderBase::Options())
    -> PositionShiftingReader<DeleteCtad<std::tuple<SrcArgs...>>>;
#endif

// Implementation details follow.

inline PositionShiftingReaderBase::PositionShiftingReaderBase(Position base_pos)
    : base_pos_(base_pos) {}

inline PositionShiftingReaderBase::PositionShiftingReaderBase(
    PositionShiftingReaderBase&& that) noexcept
    : Reader(static_cast<Reader&&>(that)), base_pos_(that.base_pos_) {}

inline PositionShiftingReaderBase& PositionShiftingReaderBase::operator=(
    PositionShiftingReaderBase&& that) noexcept {
  Reader::operator=(static_cast<Reader&&>(that));
  base_pos_ = that.base_pos_;
  return *this;
}

inline void PositionShiftingReaderBase::Reset(Closed) {
  Reader::Reset(kClosed);
  base_pos_ = 0;
}

inline void PositionShiftingReaderBase::Reset(Position base_pos) {
  Reader::Reset();
  base_pos_ = base_pos;
}

inline void PositionShiftingReaderBase::Initialize(Reader* src) {
  RIEGELI_ASSERT(src != nullptr)
      << "Failed precondition of PositionShiftingReader: null Reader pointer";
  MakeBuffer(*src);
}

inline void PositionShiftingReaderBase::SyncBuffer(Reader& src) {
  src.set_cursor(cursor());
}

inline void PositionShiftingReaderBase::MakeBuffer(Reader& src) {
  if (ABSL_PREDICT_FALSE(src.limit_pos() >
                         std::numeric_limits<Position>::max() - base_pos_)) {
    FailOverflow();
    return;
  }
  set_buffer(src.cursor(), src.available());
  set_limit_pos(src.limit_pos() + base_pos_);
  if (ABSL_PREDICT_FALSE(!src.ok())) {
    FailWithoutAnnotation(AnnotateOverSrc(src.status()));
  }
}

template <typename Src>
inline PositionShiftingReader<Src>::PositionShiftingReader(const Src& src,
                                                           Options options)
    : PositionShiftingReaderBase(options.base_pos()), src_(src) {
  Initialize(src_.get());
}

template <typename Src>
inline PositionShiftingReader<Src>::PositionShiftingReader(Src&& src,
                                                           Options options)
    : PositionShiftingReaderBase(options.base_pos()), src_(std::move(src)) {
  Initialize(src_.get());
}

template <typename Src>
template <typename... SrcArgs>
inline PositionShiftingReader<Src>::PositionShiftingReader(
    std::tuple<SrcArgs...> src_args, Options options)
    : PositionShiftingReaderBase(options.base_pos()),
      src_(std::move(src_args)) {
  Initialize(src_.get());
}

template <typename Src>
inline PositionShiftingReader<Src>::PositionShiftingReader(
    PositionShiftingReader&& that) noexcept
    : PositionShiftingReaderBase(
          static_cast<PositionShiftingReaderBase&&>(that)) {
  MoveSrc(std::move(that));
}

template <typename Src>
inline PositionShiftingReader<Src>& PositionShiftingReader<Src>::operator=(
    PositionShiftingReader&& that) noexcept {
  PositionShiftingReaderBase::operator=(
      static_cast<PositionShiftingReaderBase&&>(that));
  MoveSrc(std::move(that));
  return *this;
}

template <typename Src>
inline void PositionShiftingReader<Src>::Reset(Closed) {
  PositionShiftingReaderBase::Reset(kClosed);
  src_.Reset();
}

template <typename Src>
inline void PositionShiftingReader<Src>::Reset(const Src& src,
                                               Options options) {
  PositionShiftingReaderBase::Reset(options.base_pos());
  src_.Reset(src);
  Initialize(src_.get());
}

template <typename Src>
inline void PositionShiftingReader<Src>::Reset(Src&& src, Options options) {
  PositionShiftingReaderBase::Reset(options.base_pos());
  src_.Reset(std::move(src));
  Initialize(src_.get());
}

template <typename Src>
template <typename... SrcArgs>
inline void PositionShiftingReader<Src>::Reset(std::tuple<SrcArgs...> src_args,
                                               Options options) {
  PositionShiftingReaderBase::Reset(options.base_pos());
  src_.Reset(std::move(src_args));
  Initialize(src_.get());
}

template <typename Src>
inline void PositionShiftingReader<Src>::MoveSrc(
    PositionShiftingReader&& that) {
  if (src_.kIsStable || that.src_ == nullptr) {
    src_ = std::move(that.src_);
  } else {
    // Buffer pointers are already moved so `SyncBuffer()` is called on `*this`,
    // `src_` is not moved yet so `src_` is taken from `that`.
    SyncBuffer(*that.src_);
    src_ = std::move(that.src_);
    MakeBuffer(*src_);
  }
}

template <typename Src>
void PositionShiftingReader<Src>::Done() {
  PositionShiftingReaderBase::Done();
  if (src_.is_owning()) {
    if (ABSL_PREDICT_FALSE(!src_->Close())) {
      FailWithoutAnnotation(AnnotateOverSrc(src_->status()));
    }
  }
}

template <typename Src>
void PositionShiftingReader<Src>::SetReadAllHintImpl(bool read_all_hint) {
  if (src_.is_owning()) {
    SyncBuffer(*src_);
    src_->SetReadAllHint(read_all_hint);
    MakeBuffer(*src_);
  }
}

template <typename Src>
void PositionShiftingReader<Src>::VerifyEndImpl() {
  if (!src_.is_owning()) {
    PositionShiftingReaderBase::VerifyEndImpl();
  } else if (ABSL_PREDICT_TRUE(ok())) {
    SyncBuffer(*src_);
    src_->VerifyEnd();
    MakeBuffer(*src_);
  }
}

template <typename Src>
bool PositionShiftingReader<Src>::SyncImpl(SyncType sync_type) {
  if (ABSL_PREDICT_FALSE(!ok())) return false;
  SyncBuffer(*src_);
  bool sync_ok = true;
  if (sync_type != SyncType::kFromObject || src_.is_owning()) {
    sync_ok = src_->Sync(sync_type);
  }
  MakeBuffer(*src_);
  return sync_ok;
}

}  // namespace riegeli

#endif  // RIEGELI_BYTES_POSITION_SHIFTING_READER_H_
