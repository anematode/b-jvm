import "./style.css";
import { installRuntimeFiles, setWasmLocation } from "./bjvm";
import { caExample } from "./game-of-life";

const app = document.getElementById("app")!;

const progress = document.createElement("p");
app.appendChild(progress);

(async () => {
  setWasmLocation(
    "https://raw.githubusercontent.com/anematode/b-jvm/refs/heads/main/build/bjvm_main.wasm"
  );
  await installRuntimeFiles(
    "https://raw.githubusercontent.com/anematode/b-jvm/refs/heads/main/test",
    (loaded) =>
      (progress.innerText = `loading... (${(loaded / 555608.03).toFixed(2)}%)`),
    {}
  );
  progress.remove();

  caExample(app);
})();
