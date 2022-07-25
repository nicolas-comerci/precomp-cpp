int def(FILE *source, FILE *dest, int level, int windowbits, int memlevel);
long long def_compare(FILE *compfile, int level, int windowbits, int memlevel, long long & decompressed_bytes_used, long long decompressed_bytes_total, bool in_memory);
long long def_compare_bzip2(FILE *source, FILE *compfile, int level, long long& decompressed_bytes_used);
int def_part(FILE *source, FILE *dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out);
int def_part_skip(FILE *source, FILE *dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out, int bmp_width);
void zerr(int ret);
#ifndef PRECOMPDLL
#ifndef COMFORT
int init(int argc, char* argv[]);
#else
int init_comfort(int argc, char* argv[]);
#endif
#endif
void denit_compress();
void denit_decompress();
void denit();
bool intense_mode_is_active();
bool brute_mode_is_active();
int inf_bzip2(FILE *source, FILE *dest, long long& compressed_stream_size, long long& decompressed_stream_size);
int def_bzip2(FILE *source, FILE *dest, int level);
long long file_recompress(FILE* origfile, int compression_level, int windowbits, int memlevel, long long& decompressed_bytes_used, long long decomp_bytes_total, bool in_memory);
long long file_recompress_bzip2(FILE* origfile, int level, long long& decompressed_bytes_used, long long& decompressed_bytes_total);
void write_decompressed_data(long long byte_count, char* decompressed_file_name);
void write_decompressed_data_io_buf(long long byte_count, bool in_memory, char* decompressed_file_name);
unsigned long long compare_files(FILE* file1, FILE* file2, unsigned int pos1, unsigned int pos2);
long long compare_file_mem_penalty(FILE* file1, unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes);
long long compare_files_penalty(FILE* file1, FILE* file2, long long pos1, long long pos2);
void start_uncompressed_data();
void end_uncompressed_data();
void try_decompression_pdf(int windowbits, int pdf_header_length, int img_width, int img_height, int img_bpc);
void try_decompression_zip(int zip_header_length);
void try_decompression_gzip(int gzip_header_length);
void try_decompression_png(int windowbits);
void try_decompression_png_multi(FILE* fpng, int windowbits);
void try_decompression_gif(unsigned char version[5]);
void try_decompression_jpg(long long jpg_length, bool progressive_jpg);
void try_decompression_mp3(long long mp3_length);
void try_decompression_zlib(int windowbits);
void try_decompression_brute();
void try_decompression_swf(int windowbits);
void try_decompression_bzip2(int compression_level);
void try_decompression_base64(int gzip_header_length);

// helpers for try_decompression functions

void init_decompression_variables();
unsigned char base64_char_decode(unsigned char c);
void base64_reencode(FILE* file_in, FILE* file_out, int line_count, unsigned int* base64_line_len, long long max_in_count = 0x7FFFFFFFFFFFFFFF, long long max_byte_count = 0x7FFFFFFFFFFFFFFF);

void packjpg_mp3_dll_msg();
bool is_valid_mp3_frame(unsigned char* frame_data, unsigned char header2, unsigned char header3, int protection);
inline unsigned short mp3_calc_layer3_crc(unsigned char header2, unsigned char header3, unsigned char* sideinfo, int sidesize);
bool recompress_gif(FILE* srcfile, FILE* dstfile, unsigned char block_size, GifCodeStruct* g, GifDiffStruct* gd);
bool decompress_gif(FILE* srcfile, FILE* dstfile, long long src_pos, int& gif_length, int& decomp_length, unsigned char& block_size, GifCodeStruct* g);
void sort_comp_mem_levels();
void show_used_levels();
bool compress_file(float min_percent = 0, float max_percent = 100);
void decompress_file();
void convert_file();
long long try_to_decompress(FILE* file, int windowbits, long long& compressed_stream_size, bool& in_memory);
long long try_to_decompress_bzip2(FILE* file, int compression_level, long long& compressed_stream_size);
void try_recompress(FILE* origfile, int comp_level, int mem_level, int windowbits, long long& compressed_stream_size, long long decomp_bytes_total, bool in_memory);
void try_recompress_bzip2(FILE* origfile, int level, long long& compressed_stream_size);
void write_header();
void read_header();
void convert_header();
void fast_copy(FILE* file1, FILE* file2, long long bytecount, bool update_progress = false);
void fast_copy(FILE* file, unsigned char* mem, long long bytecount);
void fast_copy(unsigned char* mem, FILE* file, long long bytecount);
size_t own_fwrite(const void *ptr, size_t size, size_t count, FILE* stream, bool final_byte = false, bool update_lzma_progress = false);
size_t own_fread(void *ptr, size_t size, size_t count, FILE* stream);
void seek_64(FILE* f, unsigned long long pos);
unsigned long long tell_64(FILE* f);
bool file_exists(const char* filename);
#ifdef COMFORT
  bool check_for_pcf_file();
  void wait_for_key();
#endif
void error(int error_nr);
FILE* tryOpen(const char* filename, const char* mode);
long long fileSize64(const char* filename);
void print64(long long i64);
void init_temp_files();
long long get_time_ms();
void printf_time(long long t);
char get_char_with_echo();
void safe_fclose(FILE** f);
void print_work_sign(bool with_backspace);
void print_debug_percent();
void show_progress(float percent, bool use_backspaces, bool check_time);
void ctrl_c_handler(int sig);

struct recursion_result {
  bool success;
  char* file_name;
  long long file_length;
  FILE* frecurse;
};

class zLibMTF{
  struct MTFItem{
    int Next, Previous;
  };
  alignas(16) MTFItem List[81];
  int Root, Index;
public:
  zLibMTF(): Root(0), Index(0) {
    for (int i=0;i<81;i++){
      List[i].Next = i+1;
      List[i].Previous = i-1;
    }
    List[80].Next = -1;
  }
  inline int First(){
    return Index=Root;
  }
  inline int Next(){
    return (Index>=0)?Index=List[Index].Next:Index;
  }
  inline void Update(){
    if (Index==Root) return;
    
    List[ List[Index].Previous ].Next = List[Index].Next;
    if (List[Index].Next>=0)
      List[ List[Index].Next ].Previous = List[Index].Previous;
    List[Root].Previous = Index;
    List[Index].Next = Root;
    List[Root=Index].Previous = -1;
  }
};
struct recompress_deflate_result;

void write_ftempout_if_not_present(long long byte_count, bool in_memory, bool leave_open = false);
recursion_result recursion_compress(long long compressed_bytes, long long decompressed_bytes, bool deflate_type = false, bool in_memory = true);
recursion_result recursion_decompress(long long recursion_data_length);
recursion_result recursion_write_file_and_compress(const recompress_deflate_result&);

// compression-on-the-fly
enum {OTF_NONE = 0, OTF_BZIP2 = 1, OTF_XZ_MT = 2}; // uncompressed, bzip2, lzma2 multithreaded
void own_fputc(char c, FILE* f);
unsigned char fin_fgetc();
int32_t fin_fget32_little_endian();
int32_t fin_fget32();
long long fin_fget_vlint();
void fin_fget_deflate_hdr(recompress_deflate_result&, const unsigned char flags, unsigned char* hdr_data, unsigned& hdr_length, const bool inc_last);
void fin_fget_recon_data(recompress_deflate_result&);
bool fin_fget_deflate_rec(recompress_deflate_result&, const unsigned char flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last, int64_t& rec_length);
void fin_fget_uncompressed(const recompress_deflate_result&);
void fout_fputc(char c);
void fout_fput32_little_endian(int v);
void fout_fput32(int v);
void fout_fput32(unsigned int v);
void fout_fput_vlint(unsigned long long v);
void fout_fput_deflate_hdr(const unsigned char type, const unsigned char flags, const recompress_deflate_result&, const unsigned char* hdr_data, const unsigned hdr_length, const bool inc_last);
void fout_fput_recon_data(const recompress_deflate_result&);
void fout_fput_deflate_rec(const unsigned char type, const recompress_deflate_result&, const unsigned char* hdr, const unsigned hdr_length, const bool inc_last, const recursion_result& recres);
void fout_fput_uncompressed(const recompress_deflate_result&);
void init_compress_otf();
void denit_compress_otf();
void init_decompress_otf();
void denit_decompress_otf();
int auto_detected_thread_count();
int lzma_max_memory_default();

#define P_NONE 0
#define P_COMPRESS 1
#define P_DECOMPRESS 2
#define P_CONVERT 3

class RecursionContext {
  public:
    long long fin_length;
    std::string input_file_name;
    std::string output_file_name;

    std::set<long long>* intense_ignore_offsets = new std::set<long long>();
    std::set<long long>* brute_ignore_offsets = new std::set<long long>();
    int compression_otf_method = OTF_XZ_MT;
    bool decompress_otf_end = false;
    unsigned char* decomp_io_buf = NULL;

    FILE* fin = NULL;
    FILE* fout = NULL;
    FILE* ftempout = NULL;
    FILE* frecomp = NULL;
    FILE* fdecomp = NULL;
    FILE* fpack = NULL;
    FILE* fpng = NULL;
    FILE* fjpg = NULL;
    FILE* fmp3 = NULL;

    // name of temporary files
    char metatempfile[18] = "~temp00000000.dat";
    char tempfile0[19] = "~temp000000000.dat";
    char tempfile1[19] = "~temp000000001.dat";
    char tempfile2[19] = "~temp000000002.dat";
    char tempfile3[19] = "~temp000000003.dat";

    float global_min_percent = 0;
    float global_max_percent = 100;
    int comp_decomp_state = P_NONE;

    long long input_file_pos;
    unsigned char in_buf[IN_BUF_SIZE];
    int cb; // "checkbuf"
    long long saved_input_file_pos;
    long long saved_cb;

    bool compressed_data_found;

    // Uncompressed data info
    long long uncompressed_pos;
    bool uncompressed_data_in_work;
    long long uncompressed_length = -1;
    long long uncompressed_bytes_total = 0;
    long long uncompressed_bytes_written = 0;

    long long retval;

    // penalty bytes
#define MAX_PENALTY_BYTES 16384
    std::array<char, MAX_PENALTY_BYTES> penalty_bytes;
    int penalty_bytes_len = 0;
    std::array<char, MAX_PENALTY_BYTES> best_penalty_bytes;
    int best_penalty_bytes_len = 0;
    std::array<char, MAX_PENALTY_BYTES> local_penalty_bytes;

    long long identical_bytes = -1;
    long long identical_bytes_decomp = -1;
    long long best_identical_bytes = -1;
    long long best_identical_bytes_decomp = -1;

    bool anything_was_used;
    bool non_zlib_was_used;
    long long sec_time;

    // Mp3 stuff
    long long suppress_mp3_type_until[16];
    long long suppress_mp3_big_value_pairs_sum;
    long long suppress_mp3_non_zero_padbits_sum;
    long long suppress_mp3_inconsistent_emphasis_sum;
    long long suppress_mp3_inconsistent_original_bit;
    long long mp3_parsing_cache_second_frame;
    long long mp3_parsing_cache_n;
    long long mp3_parsing_cache_mp3_length;

};
