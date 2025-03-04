import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.SwingConstants;

public class ExampleWindow {

    public static void main(String[] args) {
        // Create a new JFrame (window)
        JFrame frame = new JFrame("Hello, World!");

        // Set the frame's size
        frame.setSize(300, 150);

        // Center the frame on the screen
        frame.setLocationRelativeTo(null);

        // Ensure the application exits when the frame is closed
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);

        // Create a JLabel with the "Hello, World!" text, centered horizontally
        JLabel label = new JLabel("Hello, World!", SwingConstants.CENTER);

        // Add the label to the frame's content pane
        frame.getContentPane().add(label);

        // Set the frame to be visible
        frame.setVisible(true);
    }
}