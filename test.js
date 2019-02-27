function fromHexdump(str) {
  let lines = str.split("\n");

  // remove any leading left whitespace
  lines = lines.map(line => line.trim());

  const bytes = lines.reduce((acc, line) => {
    let cols = line.split(" ");

    // remove the offset, left column
    cols.shift();

    cols = cols.filter(x => x !== "");

    const bytes = cols.map(x => parseInt(x, 16));

    acc.push(...bytes);

    return acc;
  }, []);

  return Uint8Array.from(bytes);
}

// (module
//   (func (export "a") (param i64) (result i64)
//     (get_local 0)
//    )
// )
// 00000000  00 61 73 6d 01 00 00 00  01 06 01 60 01 7e 01 7e
// 00000010  03 02 01 00 07 05 01 01  61 00 00 0a 06 01 04 00
// 00000020  20 00 0b 00 0c 04 6e 61  6d 65 02 05 01 00 01 00
// 00000030  00                                              
// 00000031

const buff = fromHexdump(`
00000000  00 61 73 6d 01 00 00 00  01 0a 02 60 00 01 7e 60
00000010  01 7e 01 7e 03 03 02 00  01 07 09 02 01 61 00 00
00000020  01 62 00 01 0a 0c 02 05  00 42 c2 00 0b 04 00 20
00000030  00 0b 00 24 04 6e 61 6d  65 01 14 02 00 07 74 65
00000040  73 74 5f 66 6e 01 08 74  65 73 74 5f 66 6e 32 02
00000050  07 02 00 00 01 01 00 00                         
00000058
`);

const m = new WebAssembly.Module(buff);
const i = new WebAssembly.Instance(m);

// (func (export "a") (result i64)
//   (i64.const 66)
// )

assert(() => i.exports.a().constructor === BigInt);
assert(() => i.exports.a() === 66n);

// (func (export "b") (param i64) (result i64)
//   (get_local 0)
// )

console.log(32768n, i.exports.b(32768n));
assert(() => i.exports.b(0n) === 0n);
assert(() => i.exports.b(-0n) === -0n);
assert(() => i.exports.b(123n) === 123n);
assert(() => i.exports.b(-123n) === -123n);

assert(() => i.exports.b(2n ** 63n + 1n) === (2n ** 63n + 1n) - 2n ** 64n);
assert(() => i.exports.b(2n ** 63n) === - (2n ** 63n));
assert(() => i.exports.b("5") === 5n);

function assert(test) {
  if (!test()) {
    console.log("FAIL -", test.toString());
    throw new Error("assert failed");
  }
  console.log("PASS -", test.toString());
}
