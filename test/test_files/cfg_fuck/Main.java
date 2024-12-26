public class Main {
    // Cursed CFG to test immediate dominator tree
    static boolean isPrime(int n) {
        if (n <= 1) {
            return false;
        }
        if (n <= 3) {
            return true;
        }
        if (n % 2 == 0 || n % 3 == 0) {
            return false;
        }
        for (int i = 5; i * i <= n; i += 6) {
            if (n % i == 0 || n % (i + 2) == 0) {
                return false;
            }
        }
        return true;
    }

    static int stardew = 0;
    static int egg = 0;
    static boolean a() {
        stardew *= 0x314159;
        stardew += 0x12;
        stardew = (stardew >>> 10) | (stardew << 22);
        return isPrime(stardew >>> 16);
    }

    static int b() {
        return egg++ % 8;
    }
    
    public static void main(String[] args) {
        int i = 0;
        while (i++ < 5000) {
            System.out.println("goat");
            c: do {
                System.out.println("sheep");
                b: while (a()) {
                    System.out.println("entry");
                    if (a()) {
                        System.out.println("baaaa");
                        switch (b()) {
                            case 2:
                                System.out.println("2");
                                continue b;
                            case 5:
                                System.out.println("5");
                                break b;
                            case 1:
                            case 6:
                                System.out.println("1");
                            case 3:
                                System.out.println("3");
                                continue c;
                            case 4:
                                System.out.println("2");
                                break;
                            default:
                                System.out.println("default");
                        }
                        System.out.println("hello");
                    } else {
                        System.out.println("bye");
                        if (a()) {
                            System.out.println("hi");
                            continue;
                        }
                        if (a()) {
                            System.out.println("skibidi");
                            break;
                        }
                    }
                    System.out.println("hiii");
                }
                System.out.println("oh boy");
            } while (a());
            System.out.println("bruh");
        }
    }
}