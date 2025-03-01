import "./style.css";
import { installRuntimeFiles, setWasmLocation } from "./bjvm";
import { caExample } from "./game-of-life";
import {makeBovineOS} from "../../bjvm2.ts";

const app = document.getElementById("app")!;

const progress = document.createElement("p");
app.appendChild(progress);

(async () => {
  const os = await makeBovineOS({
    runtimeUrl: "https://raw.githubusercontent.com/anematode/b-jvm/refs/heads/main/test",
    wasmLocation: "https://raw.githubusercontent.com/anematode/b-jvm/refs/heads/main/build/bjvm_main.wasm",
    additionalRuntimeFiles: [],
    progress: (loaded) => (progress.innerText = `loading... (${(loaded / 555608.03).toFixed(2)}%)`),
  });

  progress.remove();

  caExample(os, app);
})();
