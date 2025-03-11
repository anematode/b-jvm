
public class GetDeclaredClasses {
    public static void main(String[] args) {
        Class<?>[] classes = GetDeclaredClasses.class.getDeclaredClasses();

        for (Class<?> c : classes) {
            System.out.println(c.getName());
        }
    }

    static class Egg {}
    static class Chicken {}
    static class Omelette {}
    class Hen {}
}