class Egg {
    static {
        if (true) {
            throw new RuntimeException();
        }
    }
}

class Chicken {
    static {
        if (true) {
            throw new InternalError();
        }
    }
}

class Main {
    public static void main(String[] args) {
        try {
            try {
                new Egg();
            } catch (RuntimeException e) { // should have been wrapped in another exception
                System.out.println("Should not run");
            }
        } catch (ExceptionInInitializerError e) {
            System.out.println("Egg");
            assert(e.getCause().getClass() == RuntimeException.class);
        }

        try {
            try {
                new Chicken();
            } catch (ExceptionInInitializerError e) {
                System.out.println("Should not run");
            }
        } catch (Error e) {
            System.out.println("Chicken");
        }
    }
}
