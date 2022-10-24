
#include <stdio.h>
#include <string.h>

#include "src/flags/flags.h"
#include "src/utils/utils.h"

namespace v8 {


static int DumpFlagImplications(FILE* out) {
  i::FlagList::PrintFlagImplications(out);
  return 0;
}

}  // namespace v8

int main(int argc, char* argv[]) {
  FILE* out = stdout;
  if (argc > 2 && strcmp(argv[1], "--outfile") == 0) {
    out = fopen(argv[2], "wb");
  }
  return v8::DumpFlagImplications(out);
}