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
Compiled from "FileURLConnection.java"
public class sun.net.www.protocol.file.FileURLConnection extends sun.net.www.URLConnection {
  public void connect() throws java.io.IOException;
  public synchronized void closeInputStream() throws java.io.IOException;
  public java.util.Map<java.lang.String, java.util.List<java.lang.String>> getHeaderFields();
  public java.lang.String getHeaderField(java.lang.String);
  public java.lang.String getHeaderField(int);
  public int getContentLength();
  public long getContentLengthLong();
  public java.lang.String getHeaderFieldKey(int);
  public sun.net.www.MessageHeader getProperties();
  public long getLastModified();
  public synchronized java.io.InputStream getInputStream() throws java.io.IOException;
  public java.security.Permission getPermission() throws java.io.IOException;
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
