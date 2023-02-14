#include <stdio.h>
#include "libprecomp.h"

int main(int argc, char* argv[]) {
  char msg[256];

  if ((argc != 2) && (argc != 3)) {
    printf("\nSyntax: dlltest [d] input_file\n");
    return -1;
  }

  printf("\nCopyright message:\n");
  PrecompGetCopyrightMsg(msg);
  printf("%s\n", msg);

  if (argc == 2) {
    if (!PrecompPrecompressFile(argv[1], "~temp.pcf", msg)) {
      printf("%s\n", msg);
    } else {
      printf("File %s was precompressed successfully to ~temp.pcf.\n", argv[1]);
    }
  } else {
    if (!PrecompRecompressFile(argv[2], "out.dat", msg)) {
      printf("%s\n", msg);
    } else {
      printf("File %s was recompressed successfully to out.dat.\n", argv[2]);
    }
  }

  return 0;
}
