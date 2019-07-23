//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//

#include "table/block_based/uncompression_dict_reader.h"
#include "monitoring/perf_context_imp.h"
#include "table/block_based/block_based_table_reader.h"
#include "util/compression.h"

namespace rocksdb {

Status UncompressionDictReader::Create(
    const BlockBasedTable* table, FilePrefetchBuffer* prefetch_buffer,
    bool use_cache, bool prefetch, bool pin,
    BlockCacheLookupContext* lookup_context,
    std::unique_ptr<UncompressionDictReader>* uncompression_dict_reader) {
  assert(table);
  assert(table->get_rep());
  assert(!pin || prefetch);
  assert(uncompression_dict_reader);

  CachableEntry<BlockContents> uncompression_dict_block;
  if (prefetch || !use_cache) {
    const Status s = ReadUncompressionDictionaryBlock(
        table, prefetch_buffer, ReadOptions(), nullptr /* get_context */,
        lookup_context, &uncompression_dict_block);
    if (!s.ok()) {
      return s;
    }

    if (use_cache && !pin) {
      uncompression_dict_block.Reset();
    }
  }

  uncompression_dict_reader->reset(
      new UncompressionDictReader(table, std::move(uncompression_dict_block)));

  return Status::OK();
}

Status UncompressionDictReader::ReadUncompressionDictionaryBlock(
    const BlockBasedTable* table, FilePrefetchBuffer* prefetch_buffer,
    const ReadOptions& read_options, GetContext* get_context,
    BlockCacheLookupContext* lookup_context,
    CachableEntry<BlockContents>* uncompression_dict_block) {
  // TODO: add perf counter for compression dictionary read time

  assert(table);
  assert(uncompression_dict_block);
  assert(uncompression_dict_block->IsEmpty());

  const BlockBasedTable::Rep* const rep = table->get_rep();
  assert(rep);
  assert(!rep->compression_dict_handle.IsNull());

  const Status s = table->RetrieveBlock(
      prefetch_buffer, read_options, rep->compression_dict_handle,
      UncompressionDict::GetEmptyDict(), uncompression_dict_block,
      BlockType::kCompressionDictionary, get_context, lookup_context);

  if (!s.ok()) {
    ROCKS_LOG_WARN(
        rep->ioptions.info_log,
        "Encountered error while reading data from compression dictionary "
        "block %s",
        s.ToString().c_str());
  }

  return s;
}

Status UncompressionDictReader::GetOrReadUncompressionDictionaryBlock(
    FilePrefetchBuffer* prefetch_buffer, bool no_io, GetContext* get_context,
    BlockCacheLookupContext* lookup_context,
    CachableEntry<BlockContents>* uncompression_dict_block) const {
  assert(uncompression_dict_block);

  if (!uncompression_dict_block_.IsEmpty()) {
    uncompression_dict_block->SetUnownedValue(
        uncompression_dict_block_.GetValue());
    return Status::OK();
  }

  ReadOptions read_options;
  if (no_io) {
    read_options.read_tier = kBlockCacheTier;
  }

  return ReadUncompressionDictionaryBlock(table_, prefetch_buffer, read_options,
                                          get_context, lookup_context,
                                          uncompression_dict_block);
}

Status UncompressionDictReader::GetOrReadUncompressionDictionary(
    FilePrefetchBuffer* prefetch_buffer, bool no_io, GetContext* get_context,
    BlockCacheLookupContext* lookup_context,
    UncompressionDict* uncompression_dict) const {
  CachableEntry<BlockContents> uncompression_dict_block;
  const Status s = GetOrReadUncompressionDictionaryBlock(
      prefetch_buffer, no_io, get_context, lookup_context,
      &uncompression_dict_block);

  if (!s.ok()) {
    return s;
  }

  assert(uncompression_dict);
  assert(table_);
  assert(table_->get_rep());

  UncompressionDict dict(uncompression_dict_block.GetValue()->data,
                         table_->get_rep()->blocks_definitely_zstd_compressed);
  *uncompression_dict = std::move(dict);
  uncompression_dict_block.TransferTo(uncompression_dict);

  return Status::OK();
}

size_t UncompressionDictReader::ApproximateMemoryUsage() const {
  assert(!uncompression_dict_block_.GetOwnValue() ||
         uncompression_dict_block_.GetValue() != nullptr);
  size_t usage = uncompression_dict_block_.GetOwnValue()
             ? uncompression_dict_block_.GetValue()->ApproximateMemoryUsage()
             : 0;

#ifdef ROCKSDB_MALLOC_USABLE_SIZE
    usage += malloc_usable_size(const_cast<UncompressionDictReader*>(this));
#else
    usage += sizeof(*this);
#endif  // ROCKSDB_MALLOC_USABLE_SIZE

    return usage;
}

}  // namespace rocksdb
