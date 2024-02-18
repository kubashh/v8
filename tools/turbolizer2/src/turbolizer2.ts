// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { JsonParser } from "./json-parser";
import { TurboshaftView } from "./turboshaft-view";
import { PhaseType, TurboshaftPhase } from "./content";

window.onload = function() {

  function loadFile(txtRes: string): void {
    // If the JSON isn't properly terminated, assume compiler crashed and
    // add best-guess empty termination
    if (txtRes[txtRes.length - 2] === ",") {
      txtRes += '{"name":"disassembly","type":"disassembly","data":""}]}';
    }
//    try {

        const jsonObj = JSON.parse(txtRes);
        console.log("Json", jsonObj);

        let parser = new JsonParser;
        let content = parser.Parse(jsonObj);
        
        console.log("Content", content);

        const middlePane = document.getElementById("middle");
        let view = new TurboshaftView(middlePane);
        const firstTSPhase = content.phases.filter((p) => p.type == PhaseType.TurboshaftGraph).at(0);
        view.displayGraph((firstTSPhase as TurboshaftPhase).graph);
        view.show();

//    } catch (err) {
//      if (window.confirm("Error: Exception during load of TurboFan JSON file:\n" +
//        `error: ${err} \nDo you want to clear session storage?`)) {
//        window.sessionStorage.clear();
//      }
//    }
  }

  function initializeHandlers() {
    // The <input> form #upload-helper with type file can't be a picture.
    // We hence keep it hidden, and forward the click from the picture
    // button #upload.
    document.getElementById("upload").addEventListener("click", e => {
      document.getElementById("upload-helper").click();
      e.stopPropagation();
    });

    document.getElementById("upload-helper").addEventListener("change",
      function (this: HTMLInputElement) {
        const uploadFile = this.files && this.files[0];
        if (uploadFile) {
          const fileReader = new FileReader();
          fileReader.onload = () => {
            document.title = uploadFile.name.replace(".json", "");
            const txtRes = fileReader.result;
            if (typeof txtRes === "string") {
              loadFile(txtRes);
            }
          };
          fileReader.readAsText(uploadFile);
        }
      }
    );
  }

  initializeHandlers();
}

