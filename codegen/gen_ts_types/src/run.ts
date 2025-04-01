// Command-line tool for Node to generate TypeScript types from a set of .class or .jar files. Only public classes are
// included.

// Usage notes:
//   npx tsx generate_interfaces.ts <classpath> <output directory>
// The output directory will be structured in the same shape as the packages.
// javap must be installed and available (and of a sufficiently recent version to parse the given classfiles).

import { javap } from "./javap";
import { parseJavap } from "./parseJavap";
import { genTsTypeFromClassInfo } from "./genTsType";
import fs from "node:fs";
const fileToProcess = "test_files/Example.class";

// const javapOutput = javap(fileToProcess);

const javapOutput = `
Compiled from "Simple.java"
public class com.example.Simple {
  public com.example.Simple();
  public void simpleMethod();
}
`;

// const javapOutput = fs.readFileSync("DUMP.txt", "utf-8");

const classesInfo = parseJavap(javapOutput);

const tsTypes = classesInfo.map(genTsTypeFromClassInfo).join("\n\n");

console.log(javapOutput);
console.log(classesInfo);
// console.log(tsTypes);

fs.writeFileSync("DUMP.ts", tsTypes);
fs.writeFileSync("DUMP.json", JSON.stringify(classesInfo, null, "\t"));
