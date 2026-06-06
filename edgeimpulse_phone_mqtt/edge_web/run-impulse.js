// run-impulse.js
// Browser wrapper for Edge Impulse WebAssembly

(function (global) {
  let classifierInitialized = false;
  let initResolvers = [];

  function markInitialized() {
    classifierInitialized = true;

    while (initResolvers.length > 0) {
      const resolve = initResolvers.shift();
      resolve();
    }
  }

  function runtimeLooksReady() {
    return (
      typeof Module !== "undefined" &&
      typeof Module.run_classifier === "function" &&
      typeof Module._malloc === "function" &&
      typeof Module._free === "function" &&
      Module.HEAPU8
    );
  }

  if (typeof Module !== "undefined") {
    const oldOnRuntimeInitialized = Module.onRuntimeInitialized;

    Module.onRuntimeInitialized = function () {
      if (typeof oldOnRuntimeInitialized === "function") {
        oldOnRuntimeInitialized();
      }
      markInitialized();
    };

    setTimeout(function () {
      if (runtimeLooksReady()) {
        markInitialized();
      }
    }, 0);
  }

  class EdgeImpulseClassifier {
    constructor() {
      this._initialized = false;
    }

    init() {
      if (this._initialized) {
        return Promise.resolve();
      }

      if (classifierInitialized || runtimeLooksReady()) {
        this._initialized = true;
        classifierInitialized = true;
        return Promise.resolve();
      }

      return new Promise((resolve) => {
        initResolvers.push(() => {
          this._initialized = true;
          resolve();
        });
      });
    }

    classify(rawData, debug = false) {
      if (!this._initialized && !classifierInitialized && !runtimeLooksReady()) {
        throw new Error("Module is not initialized");
      }

      const obj = this._arrayToHeap(rawData);

      let ret = Module.run_classifier(
        obj.buffer.byteOffset,
        rawData.length,
        debug
      );

      Module._free(obj.ptr);

      if (ret.result !== 0) {
        throw new Error("Classification failed, error code: " + ret.result);
      }

      let jsResult = {
        anomaly: ret.anomaly,
        results: []
      };

      for (let cx = 0; cx < ret.size(); cx++) {
        let c = ret.get(cx);

        jsResult.results.push({
          label: c.label,
          value: c.value,
          x: c.x,
          y: c.y,
          width: c.width,
          height: c.height
        });

        c.delete();
      }

      ret.delete();

      return jsResult;
    }

    getProperties() {
      if (typeof Module.get_properties === "function") {
        return Module.get_properties();
      }
      return null;
    }

    _arrayToHeap(data) {
      let typedArray = new Float32Array(data);
      let numBytes = typedArray.length * typedArray.BYTES_PER_ELEMENT;

      let ptr = Module._malloc(numBytes);
      let heapBytes = new Uint8Array(Module.HEAPU8.buffer, ptr, numBytes);

      heapBytes.set(new Uint8Array(typedArray.buffer));

      return {
        ptr: ptr,
        buffer: heapBytes
      };
    }
  }

  global.EdgeImpulseClassifier = EdgeImpulseClassifier;
})(window);