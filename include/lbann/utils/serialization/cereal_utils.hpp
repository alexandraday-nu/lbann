////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2019, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN.
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
////////////////////////////////////////////////////////////////////////////////
#pragma once
#ifndef LBANN_UTILS_SERIALIZATION_CEREAL_UTILS_HPP_
#define LBANN_UTILS_SERIALIZATION_CEREAL_UTILS_HPP_

#include <cereal/cereal.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/xml.hpp>
#include <cereal/details/traits.hpp>

#include <cereal/types/base_class.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>

#include <lbann/utils/h2_tmp.hpp>

#if !(defined __CUDACC__ || defined __HIPCC__)
namespace lbann
{
namespace utils
{

using namespace ::cereal::traits;
using namespace ::h2::meta;

namespace details
{
/** @class IsBuiltinArchiveT
 *  @brief Predicate for testing if the given type is a built-in
 *         Cereal archive.
 */
template <typename ArchiveT>
struct IsBuiltinArchiveT;

#if !(defined DOXYGEN_SHOULD_SKIP_THIS)

template <typename ArchiveT>
struct IsBuiltinArchiveT
  : std::false_type
{};

// Add all the builtin types
template <>
struct IsBuiltinArchiveT<cereal::BinaryInputArchive> : std::true_type {};
template <>
struct IsBuiltinArchiveT<cereal::XMLInputArchive> : std::true_type {};
template <>
struct IsBuiltinArchiveT<cereal::BinaryOutputArchive> : std::true_type {};
template <>
struct IsBuiltinArchiveT<cereal::XMLOutputArchive> : std::true_type {};
#endif // defined DOXYGEN_SHOULD_SKIP_THIS
}// namespace details

/** @brief Variable template for checking that an archive is a default
 *         Cereal archive type.
*/
template <typename ArchiveT>
constexpr bool IsBuiltinArchive = details::IsBuiltinArchiveT<ArchiveT>::value;

/** @brief Variable template for checking that an archive type is
 *         marked as a text archive in Cereal.
 */
template <typename ArchiveT>
constexpr bool IsTextArchive = is_text_archive<ArchiveT>::value;

/** @brief Variable template for checking that an archive type is an
 *         "Input" archive.
 */
template <typename ArchiveT>
constexpr bool IsInputArchive =
  std::is_base_of_v<cereal::detail::InputArchiveBase, ArchiveT>;

/** @brief Variable template for checking that an archive type is an
 *         "Output" archive.
 */
template <typename ArchiveT>
constexpr bool IsOutputArchive =
  std::is_base_of_v<cereal::detail::OutputArchiveBase, ArchiveT>;

/** @brief SFINAE helper for splitting text-based and non-text-based
 *         serialization functions.
 */
template <typename ArchiveT, typename ResultT=int>
using WhenTextArchive =
  EnableWhen<IsTextArchive<ArchiveT> && IsBuiltinArchive<ArchiveT>,
             ResultT>;

/** @brief SFINAE helper for splitting text-based and non-text-based
 *         serialization functions.
 */
template <typename ArchiveT, typename ResultT=int>
using WhenNotTextArchive = EnableWhen<!IsTextArchive<ArchiveT> && IsBuiltinArchive<ArchiveT>, ResultT>;

}// namespace utils
}// namespace lbann
#endif // !(defined __CUDACC__ || defined __HIPCC__)
#endif // LBANN_UTILS_SERIALIZATION_CEREAL_UTILS_HPP_