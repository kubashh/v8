function fromHexdump(str) { let lines = str.split("\n"); lines = lines.map(line => line.trim()); const bytes = lines.reduce((acc, line) => { let cols = line.split(" "); cols.shift(); cols = cols.filter(x => x !== ""); const bytes = cols.map(x => parseInt(x, 16)); acc.push(...bytes); return acc; }, []); return Uint8Array.from(bytes); }
function assert(test) { if (!test()) { console.log("FAIL -", test.toString()); throw new Error("assert failed"); } console.log("PASS -", test.toString()); }

// (module
//   (memory 65536 65536)
//   (export "mem" (memory 0))
//   (func (export "store") (param i32 i32) (result i32)
//     (get_local 0)
//     (get_local 1)
//     (i32.store8)
//     (i32.const 1)
//   )
//   (func (export "load") (param i32) (result i32)
//     (get_local 0)
//     (i32.load8_u)
//   )
// )
const buff = fromHexdump(`
00000000  00 61 73 6d 01 00 00 00  01 0c 02 60 02 7f 7f 01
00000010  7f 60 01 7f 01 7f 03 03  02 00 01 05 08 01 01 80
00000020  80 04 80 80 04 07 16 03  03 6d 65 6d 02 00 05 73
00000030  74 6f 72 65 00 00 04 6c  6f 61 64 00 01 0a 15 02
00000040  0b 00 20 00 20 01 3a 00  00 41 01 0b 07 00 20 00
00000050  2d 00 00 0b 00 12 04 6e  61 6d 65 02 0b 02 00 02
00000060  00 00 01 00 01 01 00 00                         
00000068
`);

const m = new WebAssembly.Module(buff);
const i = new WebAssembly.Instance(m);

const { mem, store, load } = i.exports;

const pagesize = 64 * 1000;
const maxpages = 65536;
const maxoffset = maxpages * pagesize;

console.log(mem.buffer.byteLength/1024/1024/1024, "Gib");

load(2**32+666);
load(10000);
