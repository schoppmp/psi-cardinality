//
// Copyright 2020 the authors listed in CONTRIBUTORS.md
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "bloom_filter.h"
#include <cmath>
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "util/canonical_errors.h"

namespace psi_cardinality {

BloomFilter::BloomFilter(
    int num_hash_functions, std::string bits,
    std::unique_ptr<::private_join_and_compute::Context> context)
    : num_hash_functions_(num_hash_functions),
      bits_(std::move(bits)),
      context_(std::move(context)) {}

StatusOr<std::unique_ptr<BloomFilter>> BloomFilter::Create(
    double fpr, int64_t max_elements) {
  if (fpr <= 0 || fpr >= 1) {
    return ::private_join_and_compute::InvalidArgumentError(
        "`fpr` must be in (0,1)");
  }
  if (max_elements <= 0) {
    return ::private_join_and_compute::InvalidArgumentError(
        "`max_elements` must be positive");
  }
  int num_hash_functions = static_cast<int>(std::ceil(-std::log2(fpr)));
  int64_t num_bytes = static_cast<int64_t>(
      std::ceil(-1.44 * max_elements * std::log2(fpr) / 8));
  std::string bits(num_bytes, '\0');
  auto context = absl::make_unique<::private_join_and_compute::Context>();
  return absl::WrapUnique(
      new BloomFilter(num_hash_functions, std::move(bits), std::move(context)));
}

StatusOr<std::unique_ptr<BloomFilter>> BloomFilter::CreateFromBitString(
    int num_hash_functions, std::string bits) {
  if (num_hash_functions < 0) {
    return ::private_join_and_compute::InvalidArgumentError(
        "`num_hash_functions` must be positive");
  }
  if (bits.empty()) {
    return ::private_join_and_compute::InvalidArgumentError(
        "`bits` must not be empty");
  }
  auto context = absl::make_unique<::private_join_and_compute::Context>();
  return absl::WrapUnique(
      new BloomFilter(num_hash_functions, std::move(bits), std::move(context)));
}

void BloomFilter::Add(const std::string& input) {
  Add(absl::MakeConstSpan(&input, 1));
}

void BloomFilter::Add(absl::Span<const std::string> inputs) {
  for (const std::string& input : inputs) {
    for (int64_t index : Hash(input)) {
      bits_[index / 8] |= (1 << (index % 8));
    }
  }
}

bool BloomFilter::Check(const std::string& input) const {
  bool result = true;
  for (int64_t index : Hash(input)) {
    result &= ((bits_[index / 8] >> (index % 8)) & 1);
  }
  return result;
}

const std::string& BloomFilter::ToString() const { return bits_; }

int BloomFilter::NumHashFunctions() const { return num_hash_functions_; }

std::vector<int64_t> BloomFilter::Hash(const std::string& x) const {
  // Compute the number of bits (= size of the output domain) as an OpenSSL
  // BigNum.
  const int64_t num_bits = 8 * bits_.size();
  const auto bn_num_bits = context_->CreateBigNum(num_bits);

  // Compute the i-th hash function as SHA256(1 || x) + i * SHA256(2 || x)
  // (modulo num_bits).
  std::vector<int64_t> result(num_hash_functions_);
  const int64_t h1 =
      context_->CreateBigNum(context_->Sha256String(absl::StrCat(1, x)))
          .Mod(bn_num_bits)
          .ToIntValue()
          .ValueOrDie();
  const int64_t h2 =
      context_->CreateBigNum(context_->Sha256String(absl::StrCat(2, x)))
          .Mod(bn_num_bits)
          .ToIntValue()
          .ValueOrDie();
  for (int i = 0; i < num_hash_functions_; i++) {
    result[i] = (h1 + i * h2) % num_bits;
  }
  return result;
}

}  // namespace psi_cardinality