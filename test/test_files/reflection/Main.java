public class Main {
    public static void main(String[] args) {
        Class<?>[] prims = { int.class, boolean.class, byte.class, char.class, void.class, short.class, long.class, float.class, double.class, Integer.class, Integer[].class, Integer[][].class, int[].class, int[][].class, Main.class };
        for (Class<?> cls : prims) {
            System.out.print(cls.getModifiers());
            System.out.print(cls.getName());
            System.out.print(cls.getDeclaredFields().length);
            System.out.print(cls.getDeclaredMethods().length);
            System.out.print(cls.getSuperclass());
            System.out.print(cls.isPrimitive());
            System.out.print(cls.getClassLoader());
        }
    }
}
