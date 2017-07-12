// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function newBenchmark(name, handlers) {
  new BenchmarkSuite(name, [1000], [
    new Benchmark(name, false, false, 0,
                  handlers.run, handlers.setup, handlers.teardown)
  ]);
}

// ----------------------------------------------------------------------------

var result;
var foo = () => {}

newBenchmark("ProxyConstructorWithArrowFunc", {
  setup() { },
  run() {
    var proxy = new Proxy(foo, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});

// ----------------------------------------------------------------------------

class Class {};

newBenchmark("ProxyConstructorWithClass", {
  setup() { },
  run() {
    var proxy = new Proxy(Class, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});

// ----------------------------------------------------------------------------

var obj = {};

newBenchmark("ProxyConstructorWithObject", {
  setup() { },
  run() {
    var proxy = new Proxy(obj, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});

// ----------------------------------------------------------------------------

var p = new Proxy({}, {});

newBenchmark("ProxyConstructorWithProxy", {
  setup() { },
  run() {
    var proxy = new Proxy(p, {});
    result = proxy;
  },
  teardown() {
    return (typeof result == 'function');
  }
});

// ----------------------------------------------------------------------------

newBenchmark("CallProxyWithoutTrap", {
  setup() {
    var l = () => { return 42; };
    p = new Proxy(l, {});
  },
  run() {
    p();
  },
  teardown() {
    return (typeof result == 'number');
  }
});

// ----------------------------------------------------------------------------

newBenchmark("CallProxyWithTrap", {
  setup() {
    var l = () => { return 42; };
    p = new Proxy(l, {
      apply: function(target, thisArg, argumentsList) {
        return 1337;
      }
    });
  },
  run() {
    p();
  },
  teardown() {
    return (typeof result == 'number');
  }
});

var instance;
class C {
  constructor() {
  }
};

// ----------------------------------------------------------------------------

newBenchmark("ConstructProxyWithoutTrap", {
  setup() {
    p = new Proxy(C, {});
  },
  run() {
    instance = new p();
  },
  teardown() {
    return instance instanceof C;
  }
});

// ----------------------------------------------------------------------------

newBenchmark("ConstructProxyWithTrap", {
  setup() {
    p = new Proxy(C, {
      construct: function(target, argumentsList, newTarget) {
        return new C;
      }
    });
  },
  run() {
    instance = new p();
  },
  teardown() {
    return instance instanceof C;
  }
});
