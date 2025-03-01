import {BovineOS} from "../../bjvm2.ts";

export const fetchUint8Array = async (url: string) => {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to fetch data from ${url}`);
  }
  const arrayBuffer = await response.arrayBuffer();
  return new Uint8Array(arrayBuffer);
};

export const loadJar = async (os: BovineOS, name: string) => {
  const buffer = await fetchUint8Array(`/assets/${name}.jar`);
  os.FS.mkdir("/rofl", 0o777);
  os.FS.writeFile(`/rofl/${name}.jar`, buffer);
  const vm = os.makeVM<any>({ classpath: `/:/rofl/${name}.jar` });
  const Class = await vm.loadClass(name as any);
  return Class;
};
