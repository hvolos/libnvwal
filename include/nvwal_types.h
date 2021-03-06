/* 
 * Copyright 2017 Hewlett Packard Enterprise Development LP
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * 
 *   1. Redistributions of source code must retain the above copyright 
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright 
 *      notice, this list of conditions and the following disclaimer 
 *      in the documentation and/or other materials provided with the 
 *      distribution.
 *   
 *   3. Neither the name of the copyright holder nor the names of its 
 *      contributors may be used to endorse or promote products derived 
 *      from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED 
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NVWAL_TYPES_H_
#define NVWAL_TYPES_H_
/**
 * @file nvwal_types.h
 * Provides typedefs/enums/structs used in libnvwal.
 * @ingroup LIBNVWAL
 * @addtogroup LIBNVWAL
 * @{
 */

#include <stdint.h>

#include "nvwal_fwd.h"

/**
 * @brief \b Epoch, a coarse-grained timestamp libnvwal is based on.
 * @details
 * Epoch is the most important concept in libnvwal.
 * Most feature in libnvwal is provided on top of epochs.
 *
 * @par What is Epoch
 * An Epoch represents a \b user-defined duration of time.
 * Depending on the type of client applications, it might be just one transaction,
 * 10s of milliseconds, or something else. Will talk about each application later.
 * In either case, it is coarse-grained, not like a nanosecond or RDTSC.
 * An epoch contains some number of log entries, ranging from 1 to millions or more.
 * Each log entry always belongs to exactly one epoch, which represents
 * \b when the log is written and becomes durable.
 *
 * @par Log ordering invariant
 * libnvwal always writes logs out to stable storage per epoch.
 * We durably write out logs of epoch E, then epoch E+1, etc.
 * When the user requests to read back logs,
 * we always guarantee that logs are ordered by epochs.
 * Logs in the same epoch might be not in real-time order, but they
 * do have some guarantee. See the corresponding API.
 *
 * @par Concept: Durable Epoch (DE)
 * \b Durable \b Epoch (\b DE) is the epoch upto which all logs in the epoch
 * are already durably written at least to NVDIMM. No log entries in DE
 * can be newly submit to libnvwal.
 * libnvwal provides an API, nvwal_query_durable_epoch(), to atomically
 * check the current value of DE of the system.
 *
 * @par Concept: Stable Epoch (SE)
 * \b Stable \b Epoch (\b SE) is the epoch upto which all logs in the epoch
 * are already submit to libnvwal.
 * No log entries in SE can be newly submit to libnvwal.
 * SE is equal to either DE or DE+1.
 * \li When libnvwal's flusher is well catching up, SE == DE in most of the time.
 * \li When workers are waiting for libnvwal's flusher, SE == DE+1.
 * As soon as the flusher completes writing, flushing, metadata-update, etc, it will
 * bump up DE for one, thus switching to another case.
 *
 * @par Concept: Next Epoch (NE)
 * \b Next \b Epoch (\b NE) is the epoch whose logs are ready
 * to be written out to durable storages.
 * NE is equal to either SE or SE+1.
 * NE is a hint for libnvwal to utilize storage bandwidth while waiting for flusher
 * and easily keep the log ordering invariant.
 * \li When NE == SE+1, libnvwal is allowed to go ahead and write out logs in NE
 * ahead of the flusher being asked to complete SE. All the logs in SE must be
 * already written out, whether durably or not.
 * \li When NE == SE, libnvwal just writes out logs in SE to complete the epoch.
 *
 * Even if there are logs in NE + 1, libnvwal does NOT write them out.
 * We can potentially provide the epoch-by-epoch guarantee without this restriction,
 * but things are much simpler this way. libnvwal handles up to 3 or 4 epochs always.
 *
 * @par Concept: Horizon Epoch (HE)
 * \b Horizon \b Epoch (\b HE) is a mostly conceptual epoch \b we \b don't \b care.
 * HE is an epoch larger than NE. libnvwal currently does not allow submitting
 * a log in this epoch. If a worker thread does it, the thread gets synchronously waited.
 *
 * @par Examples of DE, SE, NE relationship
 * \li DE = SE = NE : Idle. Probably this happens only at start up.
 * libnvwal is not allowed to write out anything, and everything seems already durable.
 * \li DE = SE < NE : Most common case. Flusher is well catching up and writing out
 * logs in NE well ahead, fully utilizing memory/storage bandwidth.
 * \li DE < SE = NE : Second most common case. The client application has requested
 * to bump up SE and then DE. Flusher is completing remaining writes in SE and then
 * will make sure the writes are durable as well as our internal metadata is
 * atomically written.
 * \li DE < SE < NE : A bit advanced case.
 * All log writes in SE must be already done, but the flusher might be still waiting
 * for the return of fsync, etc. In the meantime, we can go ahead and start writing logs in NE.
 * To make this case useful, the flusher must have a separate fsync-thread,
 * which we currently do not have. We might do this in future, though.
 *
 * @par Current XXX Epoch
 * The above epochs are globally meaningful concepts.
 * However, libnvwal involves various threads that must be as indepent as possible
 * for scalability. We can not and should not pause all of them whenever
 * we advance the global epochs.
 * Rather, each thread often maintains its own view of currently active epochs.
 * They sometime lag behind, or even have some holes (eg. no logs received for an epoch).
 * Current XXX Epoch is the oldest epoch XXX (some module/thread) is taking care of.
 * Probably it is same as the global DE. It might be occasionally DE - 1.
 * It's asynchronously and indepently maintained by XXX to catch up with the global epochs.
 * One very helpful guarantee here is that there is no chance XXX has to handle
 * DE-2 or DE+3 thanks to how we advance DE, and that HE is not allowed to submit.
 * Based on this guarantee, many modules in libnvwal have circular windows
 * of epochs with 5 or 6 slots. They do not need to maintain any information
 * older or newer than that.
 *
 * @par Translating epochs to user applications
 * Applications have varying mappings between their own notiion of timestamp
 * and libnvwal's epochs. We carefully designed our epochs so that all
 * existing database architectures can be mapped to epochs as below.
 *
 * @par Application 1 : Epoch-based databases
 * Epoch-based databases, such as FOEDUS/SILO etc, are mapped to libnvwal's
 * epochs without any hassle. Some architecture might not differentiate DE/SE/NE,
 * but all of them are based on epochs.
 * \li Call nvwal_advance_next_epoch() whenever the new epoch might come in to the system.
 * \li Call nvwal_advance_stable_epoch() whenever the client wants to advance its own epoch
 * and then call nvwal_query_durable_epoch() and sleep/spin on it.
 *
 * @par Application 2 : LSN-based databases
 * LSN based databases, such as MySQL/PostgreSQL etc, are mapped to libnvwal
 * by considering every transaction as an epoch.
 * \li LSN-based single-log-stream databases allocate only one writer in libnvwal.
 * Thus many things are simpler.
 * \li Whenever the application requests to make a commit log record of a transaction
 * durable, call nvwal_advance_stable_epoch(). The epoch and the LSN of the commit log
 * record have one-to-one monotonically increasing relationship.
 * \li We also allow binary-searching our epoch metadata array, assuming the above relationship.
 * For this purpose, we maintain one user-defined additional version tag in our metadata.
 *
 * @par Wrap-around
 * We might run out of 2^64 epochs at some point.
 * Wrap-around is implemented as below.
 * \li We \b somehow guarantee that no two epochs
 *  in the system are more than 2^63 distant.
 * \li Thus, similarly to RFC 1982, a is after b \e iff
 * (b < a < b+2^63) or (a < b < a+2^63).
 * \li If the system is running for looong time (>2^63), we need
 * to provide a compaction tool to modify epoch values in
 * our metadata store. This piece is to be designed/implemented.
 * We will need it much much later.
 * @see nvwal_is_epoch_after()
 * @see nvwal_increment_epoch()
 * @see kNvwalInvalidEpoch
 */
typedef uint64_t  nvwal_epoch_t;

/**
 * Error code used throughout libnvwal.
 * The value is compatible to linux's error code defined in errno.h.
 *
 * We also set the errno global variable in most cases.
 */
typedef int32_t   nvwal_error_t;
/**
 * Represents a byte (8-bits) of user-given data.
 * Equivalent to char in most environments, but you'd be surprised
 * it is not in some environment.
 * We use this typedef-ed name rather than int8_t to clarify
 * the data is a user-given data (eg logs) we are managing.
 */
typedef int8_t    nvwal_byte_t;

/** Metadata store page number */
typedef uint64_t mds_page_no_t;

/** Metadata store page-file number */
typedef uint64_t mds_file_no_t;

/**
 * @brief Unique identifier for a \b Durable (or disk-resident) \b Segment.
 * @details
 * DSID identifies a log segment on disk, uniquely within the given WAL instance.
 * DSID is not unique across different WAL instances, but they are completely separate.
 *
 * A valid DSID starts with 1 (0 means null) and grows by one whenever
 * we create a new segment file on disk.
 * Each log segment is initially just NVDIMM-resident, then copied to a
 * on-disk file named "nvwal_segment_xxxxxxxx" where xxxx is a hex string of
 * DSID (always 8 characters).
 *
 */
typedef uint64_t  nvwal_dsid_t;

/**
 * @brief Debug logging severity levels 
 */
enum NvwalDebugLevel {
  kNvwalInvalidDebugLevel = 0,
  INFO = 1, 
  WARNING, 
  ERROR, 
  FATAL
};

/**
 * @brief Parameter to control how libnvwal initializes each WAL instance.
 * @see nvwal_init()
 * @details
 * This parameter flag is analogous to O_CREAT/O_TRUNCATE etc for open(2).
 * If you know the behavior of those flags in open, these flags should be
 * straightforward to you.
 */
enum NvwalInitMode {
  /**
   * Attempts to restart existing WAL instance and fails if a restart-able
   * WAL instance doesn't exist in the specified NV folder.
   * Doesn't create any new WAL instance in any circumstance.
   */
  kNvwalInitRestart = 0,
  /**
   * If there is something in the specified NV folder,
   * then it attempts to restart. Fails if it is not restartable.
   *
   * It creates a new WAL instance there
   * if and only if the folder is completely empty,
   *
   * In other words, this is a non-destructive option for create.
   */
  kNvwalInitCreateIfNotExists = 1,
  /**
   * This always creates a fresh new WAL instance and deletes
   * all files, if any, in the specified NV folder at beginning.
   *
   * In other words, this is a destructive option for create.
   */
  kNvwalInitCreateTruncate = 3,
  /**
   * Maybe something analogous to O_EXCL to handle mis-configuration
   * where the user accidentally specified the same NV-folder
   * for two WAL instances. later, later...
   */
};

/**
 * Constant values used in libnvwal.
 * To be in pure-C, we have to avoid const variables for this purpose.
 */
enum NvwalConstants {
  /**
  * This value of epoch is reserved for null/invalid/etc.
  * Thus, when we increment an epoch, we must be careful.
  * @see nvwal_increment_epoch()
  */
  kNvwalInvalidEpoch = 0,

  /**
   * Represents null for nvwal_dsid_t.
   * @see nvwal_dsid_t
   */
  kNvwalInvalidDsid = 0,

  /**
   * Represents null/invalid mds page 
   */
  kNvwalInvalidPage = 0,

  /**
   * Throughout this library, every file path must be represented within this length,
   * including null termination and serial path suffix.
   * In several places, we assume this to avoid dynamically-allocated strings and
   * simplify copying/allocation/deallocation.
   */
  kNvwalMaxPathLength = 256U,

  /**
   * nv_root_/disk_root_ must be within this length so that we can
   * append our own filenames under the folder.
   * eg nvwal_segment_xxxxxxxx.
   */
  kNvwalMaxFolderPathLength = kNvwalMaxPathLength - 32,

  /**
   * Likewise, we statically assume each WAL instance has at most this number of
   * log writers assigned.
   * Thanks to these assumptions, all structs defined in this file are PODs.
   */
  kNvwalMaxWorkers = 64U,

  /**
   * @brief Largest number of log segments being actively written.
   * @details
   * The number of active log segments is calculated from nv_quota / segment_size.
   * This constant defines the largest possible number for that.
   * If nv_quota demands more than this, nvwal_init() returns an error.
   */
  kNvwalMaxActiveSegments = 1024U,

  /**
   * Used as the segment size if the user hasn't speficied any.
   * 32MB sounds like a good place to start?
   */
  kNvwalDefaultSegmentSize = 1ULL << 25,

  /**
   * \li [oldest] : the oldest frame this writer \e might be using.
   * It is probably the global durable epoch, or occasionally older.
   * \li [oldest + 1, 2]: frames this writer \e might be using now.
   * \li [oldest + 3, 4]: guaranteed to be currently not used by this writer.
   * Thus, even when current_epoch_frame might be now bumped up for one, it is
   * safe to reset [oldest + 4]. This is why we have 5 frames.
   */
  kNvwalEpochFrameCount = 5,

  /**
   * Number of epoch-metadata information to read in cursor at a time.
   * A larger value reduces the number of accesses to MDS from cursor
   * at the cost of larger cursor objects.
   */
  kNvwalCursorEpochPrefetches = 2,

  /**
   * @brief Default page size in bytes for meta-data store.
   * 
   */
  kNvwalMdsPageSize = 1ULL << 20,


  /**
   * @brief Largest number of page files.
   */
  kNvwalMdsMaxPagefiles = 1U,

  /**
   * @brief Largest number of pages being buffered for reading.
   */
  kNvwalMdsMaxBufferPages = 1U,

  /**
   * @brief Number of epoch entries to prefetch when reading from 
   * page file.
   */
  kNvwalMdsReadPrefetch = 16U,
};

/**
 * Configurations to launch one WAL instance.
 * @note This object is a POD. It can be simply initialized by memzero,
 * copied by memcpy, and no need to free any thing.
 */
struct NvwalConfig {
  /**
   * An automatically populated version number of this libnvwal binary.
   * This value is not given by the user. Even if you set some number,
   * we will ignore it. This value is populated during initialization
   * and persisted in CF. When we find a different version in CF
   * during restart, we so far throw an error, but we might later
   * implement some auto-conversion for a minor version difference.
   */
  uint64_t libnvwal_version_;

  /**
   * Debug logging severity level
   */
  int debug_level_;

  /**
   * Null-terminated string of folder to NVDIMM storage, under which
   * this WAL instance will write out log files at first.
   * If this string is not null-terminated, nvwal_init() will return an error.
   */
  char nv_root_[kNvwalMaxPathLength];

  /**
   * Null-terminated string of folder to block storage, into which
   * this WAL instance will copy log files from NVDIMM storage.
   * If this string is not null-terminated, nvwal_init() will return an error.
   */
  char disk_root_[kNvwalMaxPathLength];

  /**
   * strnlen(nv_root). Just an auxiliary variable. Automatically
   * set during initialization.
   */
  uint16_t nv_root_len_;

  /**
   * strnlen(disk_root). Just an auxiliary variable. Automatically
   * set during initialization.
   */
  uint16_t disk_root_len_;

  /**
   * When this is a second run or later, give the definitely-durable epoch
   * as of starting.
   */
  nvwal_epoch_t resuming_epoch_;

  /**
   * Number of log writer threads on this WAL instance.
   * This value must be kNvwalMaxWorkers or less.
   * Otherwise, nvwal_init() will return an error.
   */
  uint32_t writer_count_;

  /**
   * Byte size of each segment, either on disk or NVDIMM.
   * Must be a multiply of 512.
   * If this is 0 (not set), we automatically set kNvwalDefaultSegmentSize.
   */
  uint64_t segment_size_;

  uint64_t nv_quota_;

  /** Size of (volatile) buffer for each writer-thread. */
  uint64_t writer_buffer_size_;

  /**
   * Buffer of writer_buffer_size bytes for each writer-thread,
   * allocated/deallocated by the client application.
   * The client application must provide it as of nvwal_init().
   * If any of writer_buffers[0] to writer_buffers[writer_count - 1]
   * is null, nvwal_init() will return an error.
   */
  nvwal_byte_t* writer_buffers_[kNvwalMaxWorkers];

  /** 
   * Byte size of meta-data store page.
   * Must be a multiple of 512.
   * If this is 0 (not set), we automatically set kNvwalMdsPageSize.
   */
  uint64_t mds_page_size_;
};

/**
 * Piece of NvwalControlBlock that is updated solely by
 * fsyncer. No one else will touch the cacheline to
 * cause racy file-write.
 * The size of this struct must be exactly 64 bytes.
 */
struct NvwalControlBlockFsyncerProgress {
  /**
   * The largest DSID of segment we durably copied from NV to Disk.
   * Starts with 0, and fsyncer bumps it up one by one when
   * it copies a segment to disk.
   */
  nvwal_dsid_t last_synced_dsid_;

  char cacheline_pad_[64
  - sizeof(nvwal_dsid_t)  /* last_synced_dsid_ */
  ];
};

/**
 * Piece of NvwalControlBlock that is updated solely by
 * flusher. No one else will touch the cacheline to
 * cause racy file-write.
 * The size of this struct must be exactly 64 bytes.
 */
struct NvwalControlBlockFlusherProgress {
  /**
   * DE fo this WAL instance.
   * This is the ground truth of DE in case of crash/shutdown.
   * We complete bumping up DE exactly at the time we durably bump up this variable.
   * If the increment didn't reach NV, it didn't happen.
   * Thus, we bump this up at the end of epoch-persistence procedure.
   */
  nvwal_epoch_t durable_epoch_;

  /**
   * Paged MDS Epoch (PME) is the epoch up to which
   * MDS durably wrote to disk, rather than NVDIMM.
   * We bump this up when we trigger paging on MDS.
   * We durably bump this up \b after MDS durably copies to disk
   * and \b before MDS recycles the MDS buffer.
   * Invalid epoch means MDS hasn't written to disk yet.
   */
  nvwal_epoch_t paged_mds_epoch_;

  char cacheline_pad_[64
  - sizeof(nvwal_epoch_t)  /* durable_epoch_ */
  - sizeof(nvwal_epoch_t)  /* paged_mds_epoch_ */
  ];
};

/**
 * @brief Contents of the Control File (CF)
 * @details
 * Each WAL instance has a small file called Control File (CF)
 * in the NV folder, with the name "nvwal.cf".
 * It contains:
 * \li Configuration as of starting the instance. This is immutable once started.
 * \li Tiny set of progress information. This is mutable and frequntly/durably written.
 */
struct NvwalControlBlock {

  /** Mutable, tiny progress information about flusher. One cacheline */
  struct NvwalControlBlockFlusherProgress flusher_progress_;
  /** Mutable, tiny progress information about fsyncer. One cacheline */
  struct NvwalControlBlockFsyncerProgress fsyncer_progress_;

  /** Configuration as of starting */
  struct NvwalConfig config_;

  /**
   * Makes the size of whole CF a mupliply of 512.
   * This makes it easier to use O_DIRECT.
   */
  char pad_[512 - (
    (
      sizeof(struct NvwalControlBlockFlusherProgress)
      + sizeof(struct NvwalControlBlockFsyncerProgress)
      + sizeof(struct NvwalConfig)
    )
    % 512)];
};

/**
 * @brief Represents a region in a writer's private log buffer for one epoch
 * @details
 * NvwalWriterContext maintains a circular window of this object to
 * communicate with the flusher thread, keeping track of
 * the status of one writer's volatile buffer.
 *
 * This object represents the writer's log region in one epoch,
 * consisting of two offsets. These offsets might wrap around.
 * head==offset iff there is no log in the epoch.
 * To guarantee this, we make sure the buffer never becomes really full.
 *
 * @note This object is a POD. It can be simply initialized by memzero,
 * copied by memcpy, and no need to free any thing.
 */
struct NvwalWriterEpochFrame {
  /**
   * Inclusive beginning offset in buffer marking where logs in this epoch start.
   * Always written by the writer itself only. Always read by the flusher only.
   */
  uint64_t head_offset_;

  /**
   * Exclusive ending offset in buffer marking where logs in this epoch end.
   * Always written by the writer itself only. Read by the flusher and the writer.
   */
  uint64_t tail_offset_;

  /**
   * The epoch this frame currently represents. As these frames are
   * reused in a circular fashion, it will be reset to kNvwalInvalidEpoch
   * when it is definitely not used, and then reused.
   * Loading/storing onto this variable must be careful on memory ordering.
   *
   * Always written by the writer itself only. Read by the flusher and the writer.
   */
  nvwal_epoch_t log_epoch_;

  /**
   * User metadata associated with this epoch.
   */
  uint64_t user_metadata_0_;
  uint64_t user_metadata_1_;
};

/**
 * Represents one user-defined thread that will write logs.
 *
 * @note This object is a POD. It can be simply initialized by memzero,
 * copied by memcpy, and no need to free any thing. All pointers in
 * this object just point to an existing buffer, in other words they
 * are just markers (TODO: does it have to be a pointer? how about offset).
 */
struct NvwalWriterContext {
  /** Back pointer to the parent WAL context. */
  struct NvwalContext* parent_;

  /**
   * Circular frames of this writer's offset marks.
   * @see kNvwalEpochFrameCount
   */
  struct NvwalWriterEpochFrame epoch_frames_[kNvwalEpochFrameCount];

  /**
   * Points to the newest frame this writer is using, which is also the only frame
   * this writer is now putting logs to.
   * This variable is read/written by the writer only.
   * When epoch_frames[active_frame].log_epoch == kNvwalInvalidEpoch,
   * it means no frame is currently active.
   */
  uint32_t active_frame_;

  /**
   * Sequence unique among the same parent WAL context, 0 means the first writer.
   * This is not unique among writers on different WAL contexts,
   * @invariant this == parent->writers + writer_seq_id
   */
  uint32_t writer_seq_id_;

  /**
   * This is read/written only by the writer itself, not from the flusher.
   * This is just same as the value of tail_offset in the last active frame.
   * We duplicate it here to ease maitaining the tail value, especially when we are
   * making a new frame.
   */
  uint64_t last_tail_offset_;

  /** Shorthand for parent->config.writer_buffers[writer_seq_id] */
  nvwal_byte_t* buffer_;
};

/**
 * @brief A log segment libnvwal is writing to, copying from, or reading from.
 * @details
 *
 * @note This object is a POD. It can be simply initialized by memzero,
 * copied by memcpy, and no need to free any thing \b except \b file \b descriptors and \b mmap.
 */
struct NvwalLogSegment {
  /** Back pointer to the parent WAL context. */
  struct NvwalContext* parent_;   /* +8 -> 8 */

  /**
   * mmap-ed virtual address for this segment on NVDIMM.
   * Both MAP_FAILED and NULL mean an invalid VA.
   * When it is a valid VA, libnvwal is responsible to unmap it during uninit.
   */
  nvwal_byte_t* nv_baseaddr_;   /* +8 -> 16 */

  /**
   * ID of the disk-resident segment.
   * This is bumped up without race when the segment object is recycled for next use.
   */
  nvwal_dsid_t dsid_;   /* +8 -> 24 */

  /**
   * Index of this segment in NVDIMM.
   * This is immutable once initialized.
   * @invariant this == parent_->segments_ + nv_segment_index_
   */
  uint32_t nv_segment_index_;   /* +4 -> 28 */

  /**
   * Number of pinning done by readers that are currently reading
   * from this NV-segment. While this is not zero, we must not
   * recycle this segment.
   * Value -1 is reserved for "being recycled".
   * When we recycle this segment, we CAS this from 0 to -1.
   * Readers pin it by CAS-ing from a non-negative value to the value +1.
   * Unfortunately not a simple fetch_add, but should be rare to have
   * a contention here.
   */
  int32_t nv_reader_pins_;   /* +4 -> 32 */

  /**
   * When this segment is populated and ready for copying to disk,
   * the flusher sets this variable to notify fsyncher.
   * fsyncher does nothing while this variable is 0.
   * Resets to 0 without race when the segment object is recycled for next use.
   */
  uint8_t fsync_requested_;   /* +1 -> 33 */

  /**
   * When this segment is durably copied to disk,
   * the fsyncer sets this variable to notify flusher.
   * Resets to 0 without race when the segment object is recycled for next use.
   */
  uint8_t fsync_completed_;   /* +1 -> 34 */

  uint16_t pad1_;             /* +2 -> 36 */

  /**
   * If fsyncer had any error while copying this segment to disk, the error code.
   */
  nvwal_error_t fsync_error_;  /* +4 -> 40 */

  /**
   * Number of bytes we copied so far. Read/Written only by flusher.
   * Starts with zero and resets to zero when we recycle this segment.
   */
  uint64_t written_bytes_;    /* +8 -> 48 */

  /**
   * File descriptor on NVDIMM.
   * Both -1 and 0 mean an invalid descriptor.
   * When it is a valid FD, libnvwal is responsible to close it during uninit.
   */
  int64_t nv_fd_;             /* +8 -> 56 */
  /*
  we don't retain FD on disk. it's a local variable opened/used, then immediately
  closed by the fsyncer. simpler!
  int64_t disk_fd_;
  */

  /** Each LogSegment should occupy its own cacheline (64b) */
  int64_t pad2_;              /* +8 -> 64 */
};

/**  
 * @brief Represents a metadata store page-file descriptor structure.
 */
struct NvwalMdsPageFile {
  int active_;
  struct NvwalMdsIoContext* io_;
  mds_file_no_t file_no_;
  int fd_;
};

/**
 * @brief Represents a context of a meta-data-store I/O subsystem instance.
 */
struct NvwalMdsIoContext {
  /** Nvwal context containing this context */
  struct NvwalContext* wal_;

  /** Page file descriptors */
  struct NvwalMdsPageFile files_[kNvwalMdsMaxPagefiles];

  /** Buffers */
  struct NvwalMdsBuffer* write_buffers_[kNvwalMdsMaxPagefiles];
};

/**
 * @brief Represents a volatile descriptor of a buffer frame mapped on NVRAM. 
 */
struct NvwalMdsBuffer {
  struct NvwalMdsPageFile* file_;
  mds_page_no_t page_no_;
  int dirty_;
  void* baseaddr_;
};

/**
 * @brief Represents a context of a meta-data-store buffer-manager instance.
 */
struct NvwalMdsBufferManagerContext {
  /** Nvwal context containing this context */
  struct NvwalContext* wal_;

  /** Buffers */
  struct NvwalMdsBuffer write_buffers_[kNvwalMdsMaxPagefiles];
};

/**
 * @brief Represents a context of a meta-data store instance.
 */
struct NvwalMdsContext {
  /** Nvwal context containing this context */
  struct NvwalContext* wal_;

  /** IO subsystem context */
  struct NvwalMdsIoContext io_;

  /** Buffer manager context */
  struct NvwalMdsBufferManagerContext bufmgr_;   

  /** Latest epoch */
  nvwal_epoch_t latest_epoch_;
};

/**
 * @brief Metadata about one epoch in cursor.
 * @details
 * Our cursor reads a few of this object at a time from MDS.
 * When each epoch is small (just a few logs), this buffering will reduce
 * MDS lookups.
 * When each epoch is large (eg larger than one segment), then this buffering
 * is not necessary.
 */
struct NvwalCursorEpochMetadata {
  nvwal_epoch_t epoch_;
  /** The first segment that contains any log in the epoch */
  nvwal_dsid_t start_dsid_;
  /**
   * The last segment that contains any log in the epoch.
   * Named it "last", which implies inclusive, rather than "end", which implies exclusive.
   */
  nvwal_dsid_t last_dsid_;
  /**
   * Inclusive starting byte offset in the segment
   * represented by start_dsid_.
   */
  uint32_t start_offset_;
  /**
   * Exclusive ending byte offset in the segment
   * represented by end_dsid_.
   */
  uint32_t end_offset_;
};

/**
 * @brief Represents the context of the reading API for retrieving prior
 * epoch data. Must be initialized/uninitialized via nvwal_open_log_cursor()
 * and nvwal_close_log_cursor().
 */
struct NvwalLogCursor {
  /**
   * Parent pointer. Because of this, we don't need to receive wal except
   * open, but we might revisit it later. Immutable once constructed.
   */
  struct NvwalContext* wal_;
  /**
   * The epoch the client is currently trying to read.
   * @invariant current_epoch_ == kNvwalInvalidEpoch
   * ||  start_epoch_<= current_epoch_ < end_epoch_ (in a wrap-around aware fashion)
   */
  nvwal_epoch_t current_epoch_;

  /** Inclusive first epoch requested in the range. Immutable once constructed */
  nvwal_epoch_t start_epoch_;
  /** Exclusive last epoch requested in the range. Immutable once constructed */
  nvwal_epoch_t end_epoch_;

  /**
   * Byte offset from cur_segment_data_ of log data for the current epoch.
   */
  uint64_t cur_offset_;

  /**
   * Byte length of log data for the current epoch.
   */
  uint64_t cur_len_;

  /**
   * VA-mapping of log data for the current segment.
   * We so far mmap a segment at a time, but later we will internally
   * \e stitch multiple segments via MAP_FIXED when there is a large
   * enough VA-region available.
   * But, low priority because it's anyway abstracted via next() call...
   * mmaped-length is always segment-length.
   * @invariant Is NULL if and only if current_epoch_ == kNvwalInvalidEpoch .
   */
  nvwal_byte_t* cur_segment_data_;

  /**
   * File descriptor of the disk-resident segment this cursor is
   * currently reading from. When the current segment is on NV,
   * this is 0. Instead, cur_segment_from_nv_segment_ would be ON.
   */
  int64_t cur_segment_disk_fd_;

  /**
   * Whether the cur_segment_data_ is pointing to NV-segment.
   * When true, we have pinned-down the NV-segment we are reading from,
   * thus the cursor is responsible to remove the pin.
   * When false, instead, we are responsible for closing
   * cur_segment_disk_fd_ and unmapping cur_segment_data_.
   */
  uint8_t cur_segment_from_nv_segment_;

  /**
   * DSID of the segment this cursor is currently reading from.
   */
  nvwal_dsid_t cur_segment_id_;

  /**
   * Index in fetched_epochs_ that points to current_epoch_.
   * @invariant cur_segment_data == NULL
   * || fetched_epochs_[]fetched_epochs_current_ point to current_epoch_
   */
  uint32_t fetched_epochs_current_;
  /**
   * Number of epochs we fetched into fetched_epochs_.
   */
  uint32_t fetched_epochs_count_;
  /**
   * Fetched epoch-metadata.
   */
  struct NvwalCursorEpochMetadata fetched_epochs_[kNvwalCursorEpochPrefetches];
};
/**
 * @brief Represents a context of \b one stream of write-ahead-log placed in
 * NVDIMM and secondary device.
 * @details
 * Each NvwalContext instance must be initialized by nvwal_init() and cleaned up by
 * nvwal_uninit().
 * Client programs that do distributed logging will instantiate
 * an arbitrary number of this context, one for each log stream.
 * There is no interection between two NvwalContext from libnvwal's standpoint.
 * It's the client program's responsibility to coordinate them.
 *
 * @note This object is a POD. It can be simply initialized by memzero,
 * copied by memcpy, and no need to free any thing \b except \b file \b descriptors.
 * @note To be a POD, however, this object conservatively consumes a bit large memory.
 * We recommend allocating this object on heap rather than on stack. Although
 * it's very unlikely to have this long-living object on stack anyways (unit test?)..
 */
struct NvwalContext {
  /**
   * DE of this WAL instance.
   * All logs in this epoch are durable at least on NVDIMM.
   */
  nvwal_epoch_t durable_epoch_;
  /**
   * SE of this WAL instance.
   * Writers won't submit logs in this epoch or earlier.
   * @invariant stable_ == durable_ || stable_ == durable_ + 1
   */
  nvwal_epoch_t stable_epoch_;
  /**
   * NE of this WAL instance.
   * logs in this epoch can be written to files.
   */
  nvwal_epoch_t next_epoch_;

  /**
   * All static configurations given by the user on initializing this WAL instance.
   * Once constructed, this is/must-be const. Do not modify it yourself!
   */
  struct NvwalConfig config_;

  /**
   * An auxiliary object used while restart only.
   * This holds the value of the entire config when the existing WAL instance
   * in the specified NV folder used and persisted to the Control File.
   * During restart, we grab the values first, then compare them with
   * config_ (the value given this time) to do appropriate checks/adjustments.
   * When this is a fresh-new execution, prev_config_.libnvwal_version_ is 0.
   */
  struct NvwalConfig prev_config_;

  /**
   * Maintains state of each log segment in this WAL instance.
   * Only up to log_segments_[segment_count_ - 1] are used.
   */
  struct NvwalLogSegment segments_[kNvwalMaxActiveSegments];

  /**
   * mmap-ed control block image on the control file in NV.
   */
  struct NvwalControlBlock* nv_control_block_;

  /**
   * File descriptor of the control file in NV.
   */
  int64_t nv_control_file_fd_;

  /**
   * Number of segments this WAL instance uses.
   * Immutable once constructed. Do not modify it yourself!
   * @invariant segment_count_ <= kNvwalMaxActiveSegments
   */
  uint32_t segment_count_;

  /** Index into segment[] */
  /* uint32_t cur_seg_idx_;*/
  /**
   * DSID (\b not array-index) of the NV segment the flusher is currently writing to.
   * The current array-index in segments_ can be calculated via
   *  (flusher_current_nv_segment_dsid_ - 1U) % segment_count.
   * We store the accumulative DSID rather than array-index because we sometimes
   * need to know how many cycles we went through.
   * This variable is read/written only by flusher.
   */
  nvwal_dsid_t flusher_current_nv_segment_dsid_;

  /**
   * Where the flusher started writing logs in the currently-writing epoch,
   * which is probably stable epoch. This only tells the DSID, not whether
   * it's already synced to disc or still in NVDIMM.
   */
  nvwal_dsid_t flusher_current_epoch_head_dsid_;

  /** Byte offset in the segment */
  uint64_t     flusher_current_epoch_head_offset_;

  /**
   * User defined metadata associated with currently flushed epoch.
   */
  uint64_t flusher_current_epoch_user_metadata_0_;
  uint64_t flusher_current_epoch_user_metadata_1_;


  struct NvwalWriterContext writers_[kNvwalMaxWorkers];

  /**
   * Controls the state of flusher thread.
   * One of the values in NvwalThreadState.
   */
  uint8_t flusher_thread_state_;

  /**
   * Controls the state of fsyncer thread.
   * One of the values in NvwalThreadState.
   */
  uint8_t fsyncer_thread_state_;

  /**
   * Metadata store context 
   */
  struct NvwalMdsContext mds_;

};

/*
 * A predicate closure, which includes a function pointer to the method
 * implementing the predicate, and pointer to closure state for use by
 * the predicate method.
 */ 
struct NvwalPredicateClosure {
  int (*method_)(struct NvwalPredicateClosure* predicate, uint64_t arg);
  void* state_;
};

/** @} */

#endif  /* NVWAL_TYPES_H_ */
