// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-class-fields --allow-natives-syntax

{
  class C {
    static c = 1;
  }

  assertEquals(1, C.c);

  let c = new C;
  assertEquals(undefined, c.c);
}

{
  this.c = 1;
  class C {
    static c = this.c;
  }

  assertEquals(1, C.c);

  let c = new C;
  assertEquals(undefined, c.c);
}

{
  this.c = 1;
  class C {
    static c = () => this.c;
  }

  assertEquals(1, C.c());

  let c = new C;
  assertEquals(undefined, c.c);
}

{
  this.c = 1;
  class C {
    static c = () => this.c;
  }

  let a = {c : 2 };
  assertEquals(1, C.c.call(a));

  let c = new C;
  assertEquals(undefined, c.c);
}

{
  this.foo = 1;
  class C {
    static c = function() { return this.foo; };
    static d = function() { return this.bar; };
  }

  assertEquals(undefined, C.c());

  C.bar = 1;
  assertEquals(1, C.d());

  let a = {foo : 2 };
  assertEquals(2, C.c.call(a));

  assertThrows(C.c.bind(undefined));
  let c = new C;
  assertEquals(undefined, c.c);
}

{
  let x = 'a';
  class C {
    static [x] = 1;
  }

  assertEquals(1, C.a);
  assertEquals(1, C[x]);

  x = 'b';
  assertEquals(1, C.a);
  assertEquals(undefined, C[x]);
}

{
  class C {
    static c = function() { return 1 };
  }

  assertEquals('c', C.c.name);
}

{
  d = function() { return new.target; }
  class C {
    static c = d;
  }

  assertEquals(undefined, C.c());
  assertEquals(new d, new C.c());
}

{
  class C {
    static c = () => new.target;
  }

  assertEquals(undefined, C.c());
}

{
   class C {
     static c = () => {
       let b;
       class A {
         constructor() {
           b = new.target;
         }
       };
       new A;
       assertEquals(A, b);
     }
  }

  C.c();
}

{
  class C {
    static c = new.target;
  }

  assertEquals(undefined, C.c);
}
