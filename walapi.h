/**
 * walapi.h
 *
 * Interface to a simple WAL facility optimized for NVRAM
 *
 */

typedef unsigned long long epoch_t;
typedef unsigned char byte;

typedef enum {
    SEG_UNUSED = 0,
    SEG_ACTIVE,
    SEG_COMPLETE,
    SEG_SUBMITTED,
    SEG_SYNCING,
    SEG_SYNCED
} seg_state_t;

typedef struct log_segment {
    void * nv_baseaddr;
    unsigned long long seq; /* on-disk sequence number */
    int nvram_fd;
    int disk_fd;
    seg_state_t state;
    int dir_synced;   /* is direntry of disk_fd durable? */ 
    size_t disk_offset; /* 0 for now. */
}
    
typedef struct wal_descriptor {

    epoch_t durable;
    epoch_t latest;
    
    char * nv_root;
    size_t nv_quota;
    int num_segments;

    log_segment * segment;
    
    char * log_root; /* Path to log on block storage */
    int log_root_fd;
    size_t max_log_size; /* 0 if append-only log */
    size_t log_sequence; /* Where the next segment will be written */

    byte * cur_region;        /* mmapped address of current nv segment*/
    size_t nv_offset;         /* Index into nv_region */
    int cur_seg_idx;          /* Index into segment[] */

    pthread_mutex_t mutex;
    WInfo * writer_list;

} WD;

typedef struct buffer_control_block {
    /* Updated by writer and read by flusher thread. */
    byte * tail; /* Where the writer will put new bytes */
    byte * head; /* Beginning of completely written bytes */
    byte * complete; /* End of completely written bytes */
    epoch_t latest_written;
    size_t buffer_size;
    byte * buffer;
} BufCB;

typedef struct writer_info {
    struct writer_info * next;
    BufCB * writer;
    byte * flushed; /* Everything up to this point is durable */
    byte * copied; /* Everything up to this point has been copied */
    
    /* Pending work is everything between copied and writer->complete */
} WInfo;


WD * initialize_nvwal(int numa_domain,
                      char* nv_root,
                      size_t nv_quota,
                      char* log_path,
                      int circular_log
                      );

WInfo * register_writer(WD * wal, BufCB * write_buffer);

error_t on_wal_write(WD* wal,
                     WInfo * writer,
                     byte * src,
                     size_t size,
                     epoch_t current
                     );

error_t assure_wal_space(WD * wal, WInfo * writer, size_t size);

epoch_t query_durable_epoch(WD * wal);
