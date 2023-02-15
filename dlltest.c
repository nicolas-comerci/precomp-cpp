#include <stdio.h>
#include <string.h>
#include "libprecomp.h"

FILE* open_input_file(char* in_file, bool allow_stdin) {
  FILE* fin = NULL;
  if (strcmp(in_file, "stdin") == 0) {
    if (!allow_stdin) {
      print_to_terminal("ERROR: stdin can't be used for precompression as precompression requires a seekable stream\n");
    }
    else {
      fin = stdin;
    }
  } else
  {
    fin = fopen(in_file, "rb");
    if (fin == NULL) {
      print_to_terminal("ERROR: Input file \"%s\" doesn't exist\n", in_file);
    }
  }

  return fin;
}

FILE* open_output_file(char* out_file) {
  FILE* fout = NULL;
  if (strcmp(out_file, "stdout") == 0) {
    fout = stdout;
  }
  else
  {
    fout = fopen(out_file, "a+b");
    if (fout == NULL) {
      print_to_terminal("ERROR: Output file \"%s\" doesn't exist\n", out_file);
    }
  }

  return fout;
}

int precompress_file(char* in_file, char* out_file) {
  CPrecomp* precomp_mgr = PrecompCreate();
  int filesize_err = 0;
  CRecursionContext* context = PrecompGetRecursionContext(precomp_mgr);
  context->fin_length = fileSize64(in_file, &filesize_err);
  context->compression_otf_method = 0;

  FILE* fin = open_input_file(in_file, false);
  if (fin == NULL) { return 1; }

  FILE* fout = open_output_file(out_file);
  if (fout == NULL) { return 1; }

  PrecompSetInputFile(precomp_mgr, fin, in_file);
  PrecompSetOutputFile(precomp_mgr, fout, out_file);
  return PrecompPrecompress(precomp_mgr);
}

int recompress_file(char* in_file, char* out_file) {
  CPrecomp* precomp_mgr = PrecompCreate();
  int filesize_err = 0;
  PrecompGetRecursionContext(precomp_mgr)->fin_length = fileSize64(in_file, &filesize_err);

  FILE* fin = open_input_file(in_file, true);
  if (fin == NULL) { return 1; }

  FILE* fout = open_output_file(out_file);
  if (fout == NULL) { return 1; }

  PrecompSetInputFile(precomp_mgr, fin, in_file);
  PrecompSetOutputFile(precomp_mgr, fout, out_file);
  return PrecompRecompress(precomp_mgr);
}

const int precomp_arg_count = 3;
const int recomp_arg_count = 4;

int main(int argc, char* argv[]) {
  char msg[256];

  char* input_file_name;
  char* output_file_name;
  if (argc == precomp_arg_count) {
    input_file_name = argv[1];
    output_file_name = argv[2];
  }
  else if (argc == recomp_arg_count) {
    input_file_name = argv[2];
    output_file_name = argv[3];
  }
  else {
    print_to_terminal("\nSyntax: dlltest [r] input_file output_file\n");
    return -1;
  }

  print_to_terminal("\nCopyright message:\n");
  PrecompGetCopyrightMsg(msg);
  print_to_terminal("%s\n", msg);

  int precomp_ret_code = 0;
  if (argc == precomp_arg_count) {
    print_to_terminal("\nPrecompressing %s into %s:\n", input_file_name, output_file_name);
    precomp_ret_code = precompress_file(input_file_name, output_file_name);
    if (precomp_ret_code == 0) {
      print_to_terminal("File %s was precompressed successfully to %s.\n", input_file_name, output_file_name);
    }
  } else {
    print_to_terminal("\nRecompressing %s into %s:\n", input_file_name, output_file_name);
    precomp_ret_code = recompress_file(input_file_name, output_file_name);
    if (precomp_ret_code == 0) {
      print_to_terminal("File %s was recompressed successfully to %s.\n", input_file_name, output_file_name);
    }
  }
  if (precomp_ret_code != 0) {
    print_to_terminal("ERROR: Precomp returned error code %i.\n", precomp_ret_code);
  }

  return precomp_ret_code;
}
