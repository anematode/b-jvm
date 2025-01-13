import java.lang.reflect.Method;
import java.lang.reflect.Parameter;

public class MethodParameters {
    // Example method to analyze
    public void exampleMethod(String param1, int param2, boolean param3) {
        // Do something
    }

    public static void main(String[] args) {
        try {
            // Get the class object
            Class<?> clazz = MethodParameters.class;

            // Get the method object for 'exampleMethod'
            Method method = clazz.getMethod("exampleMethod", String.class, int.class, boolean.class);

            // Get parameters of the method
            Parameter[] parameters = method.getParameters();

            // Print parameter names
            System.out.println("Parameter names for method 'exampleMethod':");
            for (Parameter parameter : parameters) {
                System.out.println(parameter.getName());
            }
        } catch (NoSuchMethodException e) {
            e.printStackTrace();
        }
    }
}
