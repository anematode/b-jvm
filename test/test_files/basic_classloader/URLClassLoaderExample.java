import java.net.URL;
import java.net.URLClassLoader;
import java.lang.reflect.Method;

public class URLClassLoaderExample {
    public static void main(String[] args) {
        try {
            // Specify the path to the JAR file
            String jarFilePath = "file:./test_files/basic_classloader/external.jar";

            // Create a URLClassLoader
            URL[] urls = {new URL(jarFilePath)};
            URLClassLoader classLoader = new URLClassLoader(urls);

            // Specify the fully qualified name of the class to load
            String className = "ExternalClass";

            // Load the class
            Class<?> loadedClass = classLoader.loadClass(className);

            // Print the loaded class name
            System.out.println("Loaded class: " + loadedClass.getName());

            // Optionally, use reflection to create an instance or call methods
            Object instance = loadedClass.getDeclaredConstructor().newInstance();
            Method method = loadedClass.getMethod("someMethod"); // replace with actual method name
            method.invoke(instance);

            Class<?> loadedClass2 = Class.forName(className, true, classLoader);
            System.out.println("Loaded class: " + loadedClass2.getName());

            Object instance2 = loadedClass2.getDeclaredConstructor().newInstance();
            Method method2 = loadedClass2.getMethod("someMethod"); // replace with actual method name
            method2.invoke(instance2);

            // Close the class loader (Java 7+)
            classLoader.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}