/*****************************************************************************

Copyright (c) 1995, 2017, Oracle and/or its affiliates. All rights reserved.
Copyright (c) 2009, Google Inc.
Copyright (c) 2017, 2020, MariaDB Corporation.

Portions of this file contain modifications contributed and copyrighted by
Google, Inc. Those modifications are gratefully acknowledged and are described
briefly in the InnoDB documentation. The contributions by Google are
incorporated with their permission, and subject to the conditions contained in
the file COPYING.Google.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file include/log0log.h
Database log

Created 12/9/1995 Heikki Tuuri
*******************************************************/

#ifndef log0log_h
#define log0log_h

#include "dyn0buf.h"
#include "log0types.h"
#include "os0event.h"
#include "os0file.h"
#include "span.h"
#include <atomic>

using st_::span;

extern ulong srv_log_buffer_size;

lsn_t buf_pool_get_oldest_modification();

/* Margin for the free space in the smallest log, before a new query
step which modifies the database, is started */

#define LOG_CHECKPOINT_FREE_PER_THREAD	(4U << srv_page_size_shift)
#define LOG_CHECKPOINT_EXTRA_FREE	(8U << srv_page_size_shift)

typedef ulint (*log_checksum_func_t)(const byte* log_block);

/** this is where redo log data is stored (no header, no checkpoints) */
static const char LOG_DATA_FILE_NAME[] = "ib_logdata";

/** creates LOG_DATA_FILE_NAME with specified size */
dberr_t create_data_file(os_offset_t size);

static const char LOG_FILE_NAME_PREFIX[] = "ib_logfile";
static const char LOG_FILE_NAME[] = "ib_logfile0";

/** Composes full path for a redo log file
@param[in]	filename	name of the redo log file
@return path with log file name*/
std::string get_log_file_path(const char *filename= LOG_FILE_NAME);

/** Returns paths for all existing log files */
std::vector<std::string> get_existing_log_files_paths();

/** Delete log file.
@param[in]	suffix	suffix of the file name */
static inline void delete_log_file(const char* suffix)
{
  auto path = get_log_file_path(LOG_FILE_NAME_PREFIX).append(suffix);
  os_file_delete_if_exists(innodb_log_file_key, path.c_str(), nullptr);
}

/***********************************************************************//**
Checks if there is need for a log buffer flush or a new checkpoint, and does
this if yes. Any database operation should call this when it has modified
more than about 4 pages. NOTE that this function may only be called when the
OS thread owns no synchronization objects except the dictionary mutex. */
UNIV_INLINE
void
log_free_check(void);
/*================*/

/** Extends the log buffer.
@param[in]	len	requested minimum size in bytes */
void log_buffer_extend(ulong len);

/** Check margin not to overwrite transaction log from the last checkpoint.
If would estimate the log write to exceed the log_capacity,
waits for the checkpoint is done enough.
@param[in]	margin	length of the data to be written */
void log_margin_checkpoint_age(ulint margin);

/** Read the current LSN. */
#define log_get_lsn() log_sys.get_lsn()

/** Read the durable LSN */
#define log_get_flush_lsn() log_sys.get_flushed_lsn()

/** Calculate the recommended highest values for lsn - last_checkpoint_lsn
and lsn - buf_get_oldest_modification().
@param[in]	file_size	requested innodb_log_file_size
@retval true on success
@retval false if the smallest log is too small to
accommodate the number of OS threads in the database server */
bool
log_set_capacity(ulonglong file_size)
	MY_ATTRIBUTE((warn_unused_result));

/** Ensure that the log has been written to the log file up to a given
log entry (such as that of a transaction commit). Start a new write, or
wait and check if an already running write is covering the request.
@param[in]	lsn		log sequence number that should be
included in the redo log file write
@param[in]	flush_to_disk	whether the written log should also
be flushed to the file system */
void log_write_up_to(lsn_t lsn, bool flush_to_disk);

/** write to the log file up to the last log entry.
@param[in]	sync	whether we want the written log
also to be flushed to disk. */
void
log_buffer_flush_to_disk(
	bool sync = true);

/** Make a checkpoint. Note that this function does not flush dirty
blocks from the buffer pool: it only checks what is lsn of the oldest
modification in the pool, and writes information about the lsn in
log file. Use log_make_checkpoint() to flush also the pool.
@return true if success, false if a checkpoint write was already running */
bool log_checkpoint();

/** Make a checkpoint */
void log_make_checkpoint();

/****************************************************************//**
Makes a checkpoint at the latest lsn and writes it to first page of each
data file in the database, so that we know that the file spaces contain
all modifications up to that lsn. This can only be called at database
shutdown. This function also writes all log in log file to the log archive. */
void
logs_empty_and_mark_files_at_shutdown(void);
/*=======================================*/

/**
Checks that there is enough free space in the log to start a new query step.
Flushes the log buffer or makes a new checkpoint if necessary. NOTE: this
function may only be called if the calling thread owns no synchronization
objects! */
void
log_check_margins(void);

/******************************************************//**
Prints info of the log. */
void
log_print(
/*======*/
	FILE*	file);	/*!< in: file where to print */
/**********************************************************************//**
Refreshes the statistics used to print per-second averages. */
void
log_refresh_stats(void);
/*===================*/

/** Offsets of a log file header */
namespace log_header
{
  /** Log file header format identifier (32-bit unsigned big-endian integer).
  Before MariaDB 10.2.2 or MySQL 5.7.9, this was called LOG_GROUP_ID
  and always written as 0, because InnoDB never supported more than one
  copy of the redo log. */
  constexpr unsigned FORMAT= 0;
  /** Redo log encryption key version (0 if not encrypted) */
  constexpr unsigned KEY_VERSION= 4;
  /** innodb_log_file_size of the circular log file (big endian).
  Some most or least significant bits may be repurposed for flags later.
  For now, the least significant 9 bits must be 0. */
  constexpr unsigned SIZE= 8;
  /** A NUL terminated string identifying the MySQL 5.7 or MariaDB 10.2+
  version that created the redo log file */
  constexpr unsigned CREATOR= 16;
  /** End of the log file creator field. */
  constexpr unsigned CREATOR_END= CREATOR + 32;

#if 1 // MDEV-14425 TODO: write here, not in the checkpoint header!
  constexpr unsigned CRYPT_MSG= CREATOR_END;
  constexpr unsigned CRYPT_KEY= CREATOR_END + MY_AES_BLOCK_SIZE;
  /** wider than info.crypt_nonce because we will no longer use the LSN */
  constexpr unsigned CRYPT_NONCE= CRYPT_KEY + MY_AES_BLOCK_SIZE;
#endif

  /** Contents of the CREATOR field */
  constexpr const char CREATOR_CURRENT[32]= "MariaDB "
    IB_TO_STR(MYSQL_VERSION_MAJOR) "." IB_TO_STR(MYSQL_VERSION_MINOR) "."
    IB_TO_STR(MYSQL_VERSION_PATCH);
};

#define LOG_CHECKPOINT_1	OS_FILE_LOG_BLOCK_SIZE
					/* first checkpoint field in the log
					header; we write alternately to the
					checkpoint fields when we make new
					checkpoints; this field is only defined
					in the first log file of a log */
#define LOG_CHECKPOINT_2	(3 * OS_FILE_LOG_BLOCK_SIZE)
					/* second checkpoint field in the log
					header */

typedef ib_mutex_t	LogSysMutex;
typedef ib_mutex_t	FlushOrderMutex;

/** Memory mapped file */
class mapped_file_t
{
public:
  mapped_file_t()= default;
  mapped_file_t(const mapped_file_t &)= delete;
  mapped_file_t &operator=(const mapped_file_t &)= delete;
  mapped_file_t(mapped_file_t &&)= delete;
  mapped_file_t &operator=(mapped_file_t &&)= delete;
  ~mapped_file_t() noexcept;

  dberr_t map(const char *path, bool read_only= false,
              bool nvme= false) noexcept;
  dberr_t unmap() noexcept;
  byte *data() noexcept { return m_area.data(); }

private:
  span<byte> m_area;
};

/** Abstraction for reading, writing and flushing file cache to disk */
class file_io
{
public:
  file_io(bool durable_writes= false) : m_durable_writes(durable_writes) {}
  virtual ~file_io() noexcept {};
  virtual dberr_t open(const char *path, bool read_only) noexcept= 0;
  virtual dberr_t rename(const char *old_path,
                         const char *new_path) noexcept= 0;
  virtual dberr_t close() noexcept= 0;
  virtual dberr_t read(os_offset_t offset, span<byte> buf) noexcept= 0;
  virtual dberr_t write(const char *path, os_offset_t offset,
                        span<const byte> buf) noexcept= 0;
  virtual dberr_t flush_data_only() noexcept= 0;

  /** Durable writes doesn't require calling flush_data_only() */
  bool writes_are_durable() const noexcept { return m_durable_writes; }

protected:
  bool m_durable_writes;
};

class file_os_io final: public file_io
{
public:
  file_os_io()= default;
  file_os_io(const file_os_io &)= delete;
  file_os_io &operator=(const file_os_io &)= delete;
  file_os_io(file_os_io &&rhs);
  file_os_io &operator=(file_os_io &&rhs);
  ~file_os_io() noexcept;

  dberr_t open(const char *path, bool read_only) noexcept final;
  bool is_opened() const noexcept { return m_fd != OS_FILE_CLOSED; }
  dberr_t rename(const char *old_path, const char *new_path) noexcept final;
  dberr_t close() noexcept final;
  dberr_t read(os_offset_t offset, span<byte> buf) noexcept final;
  dberr_t write(const char *path, os_offset_t offset,
                span<const byte> buf) noexcept final;
  dberr_t flush_data_only() noexcept final;

private:
  pfs_os_file_t m_fd{OS_FILE_CLOSED};
};

/** File abstraction + path */
class log_file_t
{
public:
  log_file_t(std::string path= "") noexcept : m_path{std::move(path)} {}

  dberr_t open(bool read_only) noexcept;
  bool is_opened() const noexcept;

  const std::string &get_path() const noexcept { return m_path; }

  dberr_t rename(std::string new_path) noexcept;
  dberr_t close() noexcept;
  dberr_t read(os_offset_t offset, span<byte> buf) noexcept;
  bool writes_are_durable() const noexcept;
  dberr_t write(os_offset_t offset, span<const byte> buf) noexcept;
  dberr_t flush_data_only() noexcept;

private:
  std::unique_ptr<file_io> m_file;
  std::string m_path;
};

/** Redo log buffer */
struct log_t{
  /** The original (not version-tagged) InnoDB redo log format */
  static constexpr uint32_t FORMAT_3_23 = 0;
  /** The MySQL 5.7.9/MariaDB 10.2.2 log format */
  static constexpr uint32_t FORMAT_10_2 = 1;
  /** The MariaDB 10.3.2 log format. */
  static constexpr uint32_t FORMAT_10_3 = 103;
  /** The MariaDB 10.4.0 log format. */
  static constexpr uint32_t FORMAT_10_4 = 104;
  /** Encrypted MariaDB redo log */
  static constexpr uint32_t FORMAT_ENCRYPTED = 1U << 31;
  /** The MariaDB 10.4.0 log format (only with innodb_encrypt_log=ON) */
  static constexpr uint32_t FORMAT_ENC_10_4 = FORMAT_10_4 | FORMAT_ENCRYPTED;
  /** The MariaDB 10.5.2 physical redo log format (encrypted or not) */
  static constexpr uint32_t FORMAT_10_5= 0x50485953;

  /** Redo log encryption key ID */
  static constexpr uint32_t KEY_ID= 1;

private:
  /** The log sequence number of the last change of durable InnoDB files */
  alignas(CACHE_LINE_SIZE)
  std::atomic<lsn_t> lsn;
  /** the first guaranteed-durable log sequence number */
  std::atomic<lsn_t> flushed_to_disk_lsn;
public:
  /** first free offset within the log buffer in use */
  size_t buf_free;
private:
  /** set when there may be need to flush the log buffer, or
  preflush buffer pool pages, or initiate a log checkpoint.
  This must hold if lsn - last_checkpoint_lsn > max_checkpoint_age. */
  std::atomic<bool> check_flush_or_checkpoint_;
public:
#if 0
  /** The sequence bit of the next record to write */
  bool sequence_bit;
#endif

	MY_ALIGNED(CACHE_LINE_SIZE)
	LogSysMutex	mutex;		/*!< mutex protecting the log */
	MY_ALIGNED(CACHE_LINE_SIZE)
	FlushOrderMutex	log_flush_order_mutex;/*!< mutex to serialize access to
					the flush list when we are putting
					dirty blocks in the list. The idea
					behind this mutex is to be able
					to release log_sys.mutex during
					mtr_commit and still ensure that
					insertions in the flush_list happen
					in the LSN order. */
	byte*		buf;		/*!< Memory of double the
					srv_log_buffer_size is
					allocated here. This pointer will change
					however to either the first half or the
					second half in turns, so that log
					write/flush to disk don't block
					concurrent mtrs which will write
					log to this buffer. Care to switch back
					to the first half before freeing/resizing
					must be undertaken. */
	bool		first_in_use;	/*!< true if buf points to the first
					half of the buffer, false
					if the second half */
	size_t		max_buf_free;	/*!< recommended maximum value of
					buf_free for the buffer in use, after
					which the buffer is flushed */
  /** Log file stuff. Protected by mutex or write_mutex. */
  struct file {
    /** format of the redo log: e.g., FORMAT_10_5 */
    uint32_t format;
    /** redo log encryption key version, or 0 if not encrypted */
    uint32_t key_version;
    /** individual log file size in bytes, including the header */
    lsn_t file_size;
  private:
    /** lsn used to fix coordinates within the log group */
    lsn_t				lsn;
    /** the byte offset of the above lsn */
    lsn_t				lsn_offset;
    /** log data file */
    log_file_t data_fd;
    /** mutex protecting appending to fd */
    alignas(CACHE_LINE_SIZE) ib_mutex_t fd_mutex;
    /** write position of fd */
    os_offset_t fd_offset;
    /** main log file */
    log_file_t fd;

  public:
    /** opens log files which must be closed prior this call */
    void open_files(std::string path);
    /** renames log file */
    dberr_t main_rename(std::string path) { return fd.rename(path); }
    os_offset_t main_file_size() const { return fd_offset; }
    /** reads from main log files */
    void main_read(os_offset_t offset, span<byte> buf);
    /** writes buffer to log file
    @param[in]	offset		offset in log file
    @param[in]	buf		buffer from which to write */
    void main_write_durable(os_offset_t offset, span<byte> buf);
    /** closes log files */
    void close_files();

    /** check that log data file is opened */
    bool data_is_opened() const  { return data_fd.is_opened(); }
    /** reads from data file */
    void data_read(os_offset_t offset, span<byte> buf);
    /** Tells whether writes require calling flush_data_only() */
    bool data_writes_are_durable() const noexcept;
    /** writes to data file */
    void data_write(os_offset_t offset, span<byte> buf);
    /** flushes OS page cache (excluding metadata!) for log file */
    void data_flush_data_only();

    /** @return whether non-physical log is encrypted */
    bool is_encrypted_old() const
    {
      ut_ad(!is_physical());
      return format & FORMAT_ENCRYPTED;
    }
    /** @return whether the physical log is encrypted */
    bool is_encrypted_physical() const
    {
      ut_ad(is_physical());
      return key_version != 0;
    }

    /** @return whether the redo log is in the physical format */
    bool is_physical() const { return format == FORMAT_10_5; }
    /** Calculate the offset of a log sequence number.
    @param[in]	lsn	log sequence number
    @return offset within the log */
    inline lsn_t calc_lsn_offset(lsn_t lsn) const;
    /** Calculate the offset of a log sequence number
    in an old redo log file (during upgrade check).
    @param[in]	lsn	log sequence number
    @return byte offset within the log */
    inline lsn_t calc_lsn_offset_old(lsn_t lsn) const;

    /** Set the field values to correspond to a given lsn. */
    void set_fields(lsn_t lsn)
    {
      lsn_t c_lsn_offset = calc_lsn_offset(lsn);
      set_lsn(lsn);
      set_lsn_offset(c_lsn_offset);
    }

    /** Read a log segment to log_sys.buf.
    @param[in,out]	start_lsn	in: read area start,
					out: the last read valid lsn
    @param[in]		end_lsn		read area end
    @return	whether no invalid blocks (e.g checksum mismatch) were found */
    bool read_log_seg(lsn_t* start_lsn, lsn_t end_lsn) noexcept;

    /** Initialize the redo log buffer. */
    void create();

    /** Close the redo log buffer. */
    void close() { close_files(); mutex_free(&fd_mutex); }
    void set_lsn(lsn_t a_lsn);
    lsn_t get_lsn() const { return lsn; }
    void set_lsn_offset(lsn_t a_lsn);
    lsn_t get_lsn_offset() const { return lsn_offset; }

    /** Append data to ib_logfile0 */
    dberr_t append_to_main_log(span<const byte> buf) noexcept;
  } log;

	/** The fields involved in the log buffer flush @{ */

	size_t		buf_next_to_write;/*!< first offset in the log buffer
					where the byte content may not exist
					written to file, e.g., the start
					offset of a log record catenated
					later; this is advanced when a flush
					operation is completed to all the log
					groups */
	lsn_t		write_lsn;	/*!< last written lsn */
	lsn_t		current_flush_lsn;/*!< end lsn for the current running
					write + flush operation */
	std::atomic<size_t> pending_flushes; /*!< system calls in progress */
	std::atomic<size_t> flushes;	/*!< system calls counter */

	ulint		n_log_ios;	/*!< number of log i/os initiated thus
					far */
	ulint		n_log_ios_old;	/*!< number of log i/o's at the
					previous printout */
	time_t		last_printout_time;/*!< when log_print was last time
					called */
	/* @} */

	/** Fields involved in checkpoints @{ */
	lsn_t		log_capacity;	/*!< capacity of the log; if
					the checkpoint age exceeds this, it is
					a serious error because it is possible
					we will then overwrite log and spoil
					crash recovery */
	lsn_t		max_modified_age_async;
					/*!< when this recommended
					value for lsn -
					buf_pool_get_oldest_modification()
					is exceeded, we start an
					asynchronous preflush of pool pages */
	lsn_t		max_modified_age_sync;
					/*!< when this recommended
					value for lsn -
					buf_pool_get_oldest_modification()
					is exceeded, we start a
					synchronous preflush of pool pages */
	lsn_t		max_checkpoint_age_async;
					/*!< when this checkpoint age
					is exceeded we start an
					asynchronous writing of a new
					checkpoint */
	lsn_t		max_checkpoint_age;
					/*!< this is the maximum allowed value
					for lsn - last_checkpoint_lsn when a
					new query step is started */
	ib_uint64_t	next_checkpoint_no;
					/*!< next checkpoint number */
	lsn_t		last_checkpoint_lsn;
					/*!< latest checkpoint lsn */
	lsn_t		next_checkpoint_lsn;
					/*!< next checkpoint lsn */
	ulint		n_pending_checkpoint_writes;
					/*!< number of currently pending
					checkpoint writes */
	/* @} */

private:
  bool m_initialised;
public:
  /**
    Constructor.

    Some members may require late initialisation, thus we just mark object as
    uninitialised. Real initialisation happens in create().
  */
  log_t(): m_initialised(false) {}

  /** @return whether the non-physical redo log is encrypted */
  bool is_encrypted_old() const { return log.is_encrypted_old(); }
  /** @return whether the physical redo log is encrypted */
  bool is_encrypted_physical() const { return log.is_encrypted_physical(); }
  /** @return whether the redo log is in the physical format */
  bool is_physical() const { return log.is_physical(); }

  bool is_initialised() const { return m_initialised; }

  lsn_t get_lsn() const { return lsn.load(std::memory_order_relaxed); }
  void set_lsn(lsn_t lsn) { this->lsn.store(lsn, std::memory_order_relaxed); }

  lsn_t get_flushed_lsn() const
  { return flushed_to_disk_lsn.load(std::memory_order_relaxed); }
  void set_flushed_lsn(lsn_t lsn)
  { flushed_to_disk_lsn.store(lsn, std::memory_order_relaxed); }

  bool check_flush_or_checkpoint() const
  { return check_flush_or_checkpoint_.load(std::memory_order_relaxed); }
  void set_check_flush_or_checkpoint(bool flag= true)
  { check_flush_or_checkpoint_.store(flag, std::memory_order_relaxed); }

  size_t get_pending_flushes() const
  {
    return pending_flushes.load(std::memory_order_relaxed);
  }

  size_t get_flushes() const
  {
    return flushes.load(std::memory_order_relaxed);
  }

  /** Initialise the redo log subsystem. */
  void create();

  /** Shut down the redo log subsystem. */
  void close();

  /** Initiate a write of the log buffer to the file if needed.
  @param flush  whether to initiate a durable write */
  inline void initiate_write(bool flush) noexcept
  {
    const lsn_t lsn= get_lsn();
    if (!flush || get_flushed_lsn() < lsn)
      log_write_up_to(lsn, flush);
  }

  /** Append data to ib_logfile0 */
  dberr_t append_to_main_log(span<const byte> buf) noexcept
  { return log.append_to_main_log(buf); }

  /** Reserve space in the log buffer for appending data.
  @param size   upper limit of the length of the data to append(), in bytes */
  void append_prepare(size_t size) noexcept;

  /** Append a string of bytes to the redo log.
  @param s     string of bytes
  @param size  length of str, in bytes */
  void append(const void *s, size_t size) noexcept
  {
    ut_ad(mutex_own(&mutex));
    memcpy(buf + buf_free, s, size);
    buf_free+= size;
    ut_ad(buf_free <= size_t{srv_log_buffer_size});
  }

  /** Finish appending data to the log. */
  void append_finish(lsn_t end_lsn) noexcept
  {
    ut_ad(mutex_own(&mutex));
    set_lsn(end_lsn);

    const bool set_check= buf_free > max_buf_free;
    if (set_check)
      set_check_flush_or_checkpoint();

    lsn_t checkpoint_age= end_lsn - last_checkpoint_lsn;

    if (UNIV_UNLIKELY(checkpoint_age >= log_capacity))
      overwrite_warning(checkpoint_age, log_capacity);

    if (set_check || check_flush_or_checkpoint() ||
        checkpoint_age <= max_modified_age_sync)
      return;

    const lsn_t oldest_lsn= buf_pool_get_oldest_modification();
    if (!oldest_lsn || lsn - oldest_lsn > max_modified_age_sync ||
        checkpoint_age > max_checkpoint_age_async)
      set_check_flush_or_checkpoint();
  }

private:
  /** Display a warning that the log tail is overwriting the head,
  making the server crash-unsafe. */
  ATTRIBUTE_COLD static void overwrite_warning(lsn_t age, lsn_t capacity);
};

/** Redo log system */
extern log_t	log_sys;
#ifdef UNIV_DEBUG
extern bool log_write_lock_own();
#endif

/** Gets the log capacity. It is OK to read the value without
holding log_sys.mutex because it is constant.
@return log capacity */
inline lsn_t log_get_capacity(void) { return log_sys.log_capacity; }

/** Calculate the offset of a log sequence number.
@param[in]     lsn     log sequence number
@return offset within the log */
inline lsn_t log_t::file::calc_lsn_offset(lsn_t lsn) const
{
  ut_ad(this == &log_sys.log);
  /* The lsn parameters are updated while holding both the mutexes
  and it is ok to have either of them while reading */
  ut_ad(log_sys.mutex.is_owned() || log_write_lock_own());
  const lsn_t size= file_size;
  lsn_t l= lsn - this->lsn;
  if (longlong(l) < 0)
  {
    l= lsn_t(-longlong(l)) % size;
    l= size - l;
  }

  l+= lsn_offset;
  l%= size;
  return l;
}

inline void log_t::file::set_lsn(lsn_t a_lsn) {
      ut_ad(log_sys.mutex.is_owned() || log_write_lock_own());
      lsn = a_lsn;
}

inline void log_t::file::set_lsn_offset(lsn_t a_lsn) {
      ut_ad(log_sys.mutex.is_owned() || log_write_lock_own());
      lsn_offset = a_lsn;
}

/** Test if flush order mutex is owned. */
#define log_flush_order_mutex_own()			\
	mutex_own(&log_sys.log_flush_order_mutex)

/** Acquire the flush order mutex. */
#define log_flush_order_mutex_enter() do {		\
	mutex_enter(&log_sys.log_flush_order_mutex);	\
} while (0)
/** Release the flush order mutex. */
# define log_flush_order_mutex_exit() do {		\
	mutex_exit(&log_sys.log_flush_order_mutex);	\
} while (0)

/** Test if log sys mutex is owned. */
#define log_mutex_own() mutex_own(&log_sys.mutex)


/** Acquire the log sys mutex. */
#define log_mutex_enter() mutex_enter(&log_sys.mutex)


/** Release the log sys mutex. */
#define log_mutex_exit() mutex_exit(&log_sys.mutex)

#include "log0log.ic"

#endif
