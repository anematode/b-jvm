import java.util.Random;

public class RandomTest {
    public static void main(String[] args) {
        // Create an instance of the Random class
        Random random = new Random();

        // Generate a random integer
        int randomInt = random.nextInt();
        System.out.println("Random Integer: " + randomInt);

        // Generate a random integer between 0 (inclusive) and a specified value (exclusive)
        int randomIntRange = random.nextInt(100); // Range: 0 to 99
        System.out.println("Random Integer (0 to 99): " + randomIntRange);

        // Generate a random double between 0.0 (inclusive) and 1.0 (exclusive)
        double randomDouble = random.nextDouble();
        System.out.println("Random Double: " + randomDouble);

        // Generate a random float between 0.0f (inclusive) and 1.0f (exclusive)
        float randomFloat = random.nextFloat();
        System.out.println("Random Float: " + randomFloat);

        // Generate a random boolean
        boolean randomBoolean = random.nextBoolean();
        System.out.println("Random Boolean: " + randomBoolean);

        // Generate a random integer within a custom range (e.g., 50 to 150)
        int lowerBound = 50;
        int upperBound = 150;
        int randomIntCustomRange = lowerBound + random.nextInt(upperBound - lowerBound);
        System.out.println("Random Integer (50 to 150): " + randomIntCustomRange);
    }
}