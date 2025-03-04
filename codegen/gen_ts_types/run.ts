// Command-line tool for Node to generate TypeScript types from a set of .class or .jar files. Only public classes are
// included.

// Usage notes:
//   npx tsx generate_interfaces.ts <classpath> <output directory>
// The output directory will be structured in the same shape as the packages.
// javap must be installed and available (and of a sufficiently recent version to parse the given classfiles).

// @ts-ignore
import * as util from "util";

import { javap } from "./javap";
import { parseJavap } from "./parseJavap";

const fileToProcess = "test_files/Example.class";

const javapOutput = javap(fileToProcess);

const classInfo = parseJavap(javapOutput);

console.log(util.inspect(classInfo, false, null, true));
