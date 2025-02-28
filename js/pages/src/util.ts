import { VM } from "./bjvm";

export const fetchUint8Array = async (url: string) => {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to fetch data from ${url}`);
  }
  const arrayBuffer = await response.arrayBuffer();
  return new Uint8Array(arrayBuffer);
};

export const loadJar = async (name: string) => {
  const buffer = await fetchUint8Array(`/assets/${name}.jar`);
  VM.module.FS.mkdir("/rofl", 0o777);
  VM.module.FS.writeFile(`/rofl/${name}.jar`, buffer);
  const vm = await VM.create({ classpath: `/:/rofl/${name}.jar` });
  const mainThread = vm.createThread();
  const Class = await mainThread.loadClass(name as any);
  return Class;
};
