import { loadJar } from "./util";
import {BovineOS} from "../../bjvm2.ts";

/**
 * Cellular automaton example featuring Conway's Game of Life
 */
export const caExample = async (os: BovineOS, app: HTMLElement) => {
  const CellularAutomaton = await loadJar(os, "Conway2D");

  const canvas = document.createElement("canvas");
  app.appendChild(canvas);

  const ctx = canvas.getContext("2d");
  if (!ctx) {
    return;
  }

  const timer = document.createElement("p");
  app.appendChild(timer);

  const WIDTH = 512;
  const HEIGHT = 512;
  const GRID_SIZE = 100;
  const CELL_SIZE = WIDTH / GRID_SIZE;

  canvas.width = WIDTH;
  canvas.height = HEIGHT;

  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, WIDTH, HEIGHT);

  let arr: number[] = [];

  try {
    arr = JSON.parse(await CellularAutomaton.enjoyment());
    console.log(arr);
  } catch (e) {
    console.log("error", e);
    return;
  }

  const draw = () => {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, WIDTH, HEIGHT);
    ctx.fillStyle = "#fff";

    for (let i = 0; i < GRID_SIZE; i++) {
      for (let j = 0; j < GRID_SIZE; j++) {
        if (arr[i * GRID_SIZE + j] === 1) {
          ctx.fillRect(i * CELL_SIZE, j * CELL_SIZE, CELL_SIZE, CELL_SIZE);
        }
      }
    }
  };

  draw();

  const startTime = performance.now();
  setInterval(() => {
    timer.innerText = `Time: ${(performance.now() - startTime).toString()}ms`;
  }, 1);

  async function frame() {
    if (!ctx) {
      return;
    }
    arr = JSON.parse(await CellularAutomaton.step());
    draw();
    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
};
