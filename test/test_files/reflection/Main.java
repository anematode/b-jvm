public class Main {
    public static void main(String[] args) {
        Class<?>[] clses = { int.class, boolean.class, byte.class, char.class, void.class, short.class, long.class, float.class, double.class, Integer.class, Integer[].class, Integer[][].class, int[].class, int[][].class, java.io.Serializable.class };
        for (Class<?> cls : clses) {
            System.out.println(cls.getName());
            System.out.print(cls.getModifiers());
            System.out.print(cls.getComponentType());
            System.out.print(cls.isArray());
            System.out.print(cls.isInterface());
            System.out.print(cls.isInstance((Integer) 1));
            System.out.print(cls.isInstance(new Integer[2]));
            System.out.print(cls.isInstance(new int[2]));
            System.out.print(cls.isInstance(new int[2][2]));
            System.out.print(cls.getDeclaredFields().length);
            System.out.print(cls.getDeclaredMethods().length);
            System.out.print(cls.getSuperclass());
            System.out.print(cls.isPrimitive());
            System.out.print(cls.getClassLoader());
        }
    }
}
