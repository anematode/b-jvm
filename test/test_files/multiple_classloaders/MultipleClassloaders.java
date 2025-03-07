import java.net.URL;
import java.net.URLClassLoader;
import java.net.MalformedURLException;

public class MultipleClassloaders {
    public static void main(String[] args) {
        // Load "DynamicallyLoaded.java" from the JAR file "test/test_files/url-classloader/test.jar"
        for (int i = 0; i < 10; ++i) {
            try {
                URL url = new URL("file:./test_files/multiple_classloaders/test.jar");
                URLClassLoader loader = new URLClassLoader(new URL[] {url});
                Class<?> clazz = loader.loadClass("DynamicallyLoaded");
                clazz.getMethod("main", String[].class).invoke(null, (Object) new String[0]);
            } catch (MalformedURLException e) {
                e.printStackTrace();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }
}