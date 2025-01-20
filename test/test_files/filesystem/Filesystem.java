import java.io.*;
import java.nio.file.*;

public class Filesystem {
    public static void main(String[] args) {
        var myPath = Path.of("jdk23");

        System.out.println(myPath.getFileSystem().getClass().getSimpleName());
    }
}
