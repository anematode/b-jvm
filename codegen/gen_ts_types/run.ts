// Command-line tool for Node to generate TypeScript types from a set of .class or .jar files. Only public classes are
// included.

// Usage notes:
//   npx tsx generate_interfaces.ts <classpath> <output directory>
// The output directory will be structured in the same shape as the packages.
// javap must be installed and available (and of a sufficiently recent version to parse the given classfiles).

import { javap } from "./javap";
import { parseJavap } from "./parseJavap";
import { genTsTypeFromClassInfo } from "./genTsType";

const fileToProcess = "test_files/Example.class";

// const javapOutput = javap(fileToProcess);

const javapOutput = `
Compiled from "Conway2D.java"
public class Conway2D {
  public static GameOfLife game;
  public Conway2D();
  public static void main(java.lang.String[]);
  public static java.lang.String enjoyment();
  public static java.lang.String step();
}
Compiled from "Conway2D.java"
class GameOfLife {
  public GameOfLife();
  public GameOfLife(int, int);
  public void iterate();
  public void randomSeed();
  public byte[] getData();
}

Compiled from "FtpClient.java"
class sun.net.ftp.impl.FtpClient$MLSxParser implements sun.net.ftp.FtpDirParser {
  public sun.net.ftp.FtpDirEntry parseLine(java.lang.String);
}

Compiled from "LauncherHelper.java"
final class sun.launcher.LauncherHelper$SizePrefix extends java.lang.Enum<sun.launcher.LauncherHelper$SizePrefix> {
  public static final sun.launcher.LauncherHelper$SizePrefix KILO;
  public static final sun.launcher.LauncherHelper$SizePrefix MEGA;
  public static final sun.launcher.LauncherHelper$SizePrefix GIGA;
  public static final sun.launcher.LauncherHelper$SizePrefix TERA;
  public static sun.launcher.LauncherHelper$SizePrefix[] values();
  public static sun.launcher.LauncherHelper$SizePrefix valueOf(java.lang.String);
}
`;

const classesInfo = parseJavap(javapOutput);

const tsTypes = classesInfo.map(genTsTypeFromClassInfo).join("\n\n");

console.log(classesInfo);
console.log(tsTypes);
