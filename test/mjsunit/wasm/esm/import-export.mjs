// Flags: --experimental-wasm-esm

import {ab} from "./import-export.wasm";
// import {b} from "./a.mjs";

(function TestImported() {
  console.log(ab.value);
  // assertEquals(ab.value, b);
})();
