
public class Main {

    public static void main(String[] args) {
        Item src = initializedItem();
        incrementInt(src);

        System.out.println("src is:");
        describe(src);
        System.out.println("original int val: " + src.intVal);

        Item copy = new Item();

        copyFields(src, copy);

        System.out.println("copied int val: " + copy.intVal);

        Item copy2 = null;

        copyFields(src, copy2); // should print all the NPE messages

        Item result = new Item();

        copyFields(copy2, result); // should print all the NPE messages

        System.out.println("result is:");
        describe(result);
        System.out.println("result int val: " + result.intVal); // should be 0, default value
        System.out.println("result obj val: " + result.objVal); // should be null
    }

    static class Item {
        byte byteVal;
        short shortVal;
        int intVal;
        long longVal;
        float floatVal;
        double doubleVal;
        char charVal = 'e';
        boolean boolVal;
        Object objVal;
    }

    static Item initializedItem() {
        Item result = new Item();
        result.byteVal = 1;
        result.shortVal = 1;
        result.intVal = 1;
        result.longVal = 1;
        result.floatVal = 1;
        result.doubleVal = 1;
        result.charVal = 'a';
        result.boolVal = true;
        result.objVal = "hello";
        return result;
    }

    static void copyByte(Item src, Item dest) {
        dest.byteVal = src.byteVal;
    }

    static void copyShort(Item src, Item dest) {
        dest.shortVal = src.shortVal;
    }

    static void copyInt(Item src, Item dest) {
        dest.intVal = src.intVal;
    }

    static void copyLong(Item src, Item dest) {
        dest.longVal = src.longVal;
    }

    static void copyFloat(Item src, Item dest) {
        dest.floatVal = src.floatVal;
    }

    static void copyDouble(Item src, Item dest) {
        dest.doubleVal = src.doubleVal;
    }

    static void copyChar(Item src, Item dest) {
        dest.charVal = src.charVal;
    }

    static void copyBool(Item src, Item dest) {
        dest.boolVal = src.boolVal;
    }

    static void copyObj(Item src, Item dest) {
        dest.objVal = src.objVal;
    }

    static void incrementInt(Item item) {
        item.intVal++;
    }

    static void copyFields(Item src, Item dest) {
        try {
            copyByte(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyShort(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyInt(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyLong(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyFloat(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyDouble(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyChar(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyBool(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }

        try {
            copyObj(src, dest);
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
        }
    }

    static void describe(Item item) {
        System.out.println("byte: " + item.byteVal);
        System.out.println("short: " + item.shortVal);
        System.out.println("int: " + item.intVal);
        System.out.println("long: " + item.longVal);
        System.out.println("float: " + item.floatVal);
        System.out.println("double: " + item.doubleVal);
        System.out.println("char: " + item.charVal);
        System.out.println("bool: " + item.boolVal);
        System.out.println("obj: " + item.objVal);
    }
}