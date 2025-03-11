// Class loaders which try to avoid the delegation model and instead do something cursed
public class BadClassloaders {
    public static void main(String[] args) throws ClassNotFoundException {
        ClassLoader returnsNull = new ClassLoader() {
            @Override
            public Class<?> loadClass(String name) throws ClassNotFoundException {
                return null;
            };
        };

        try {
            Class.forName("java.lang.String", true, returnsNull);
        } catch (ClassNotFoundException e) {
            System.out.println("Message:" + e.getMessage());
        }

        ClassLoader throwsError = new ClassLoader() {
            @Override
            public Class<?> loadClass(String name) throws ClassNotFoundException {
                throw new InternalError();
            };
        };

        try {
            Class.forName("java.lang.String", true, throwsError);
            System.out.println("unreachable");
        } catch (InternalError e) {
        }

        ClassLoader triesToDefineBuiltin = new ClassLoader() {
            @Override
            public Class<?> loadClass(String name) throws ClassNotFoundException {
                return defineClass(name, new byte[0], 0, 0);
            };
        };

        try {
            Class.forName("java.lang.String", true, triesToDefineBuiltin);
            System.out.println("unreachable");
        } catch (SecurityException e) {
            System.out.println("Caught security exception");
        }
    }
}