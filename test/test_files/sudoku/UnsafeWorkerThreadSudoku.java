import java.io.*;
// import java.util.concurrent.atomic.AtomicReference;
import jdk.internal.misc.Unsafe;
import java.lang.reflect.Field;

public class UnsafeWorkerThreadSudoku {
//     private static final AtomicReference<Board> puzzle = new AtomicReference<>(); // todo: not implemented yet
    private static final Unsafe unsafe = Unsafe.getUnsafe();
    private static Board puzzle = null; // not thread safe, but it should work on bjvm
    private static final Board END_EXECUTION = new Board(); // sentinel value

    private static final Object base;
    private static final long puzzleOffset;

    static {
        try {
            Field field = UnsafeWorkerThreadSudoku.class.getDeclaredField("puzzle");
            // field.setAccessible(true);
            base = unsafe.staticFieldBase(field);
            puzzleOffset = unsafe.staticFieldOffset(field);
        } catch (Exception e) {
            throw new Error(e);
        }
    }

    public static void runWorker() {
        Solver solver = new Solver();
        while (!Thread.interrupted()) {
            Board board;
            while ((board = (Board) unsafe.getAndSetReference(base, puzzleOffset, null)) == null) {
            // while ((board = puzzle.getAndSet(null)) == null) {
                if (Thread.interrupted()) return;
                Thread.yield(); // spin lock lol
            }

            if (board == END_EXECUTION) return;

            Board result = solver.solve(board);
            if (!result.isSolved()) throw new IllegalArgumentException("Failed to solve " + board);
            System.out.println(result);
        }
    }

	public static void main(String[] args) throws Exception {
        Thread worker = new Thread(UnsafeWorkerThreadSudoku::runWorker);
        worker.start();

        try (BufferedReader reader = new BufferedReader(new FileReader("test_files/sudoku/sudoku.txt"))) {
            String line;
            while ((line = reader.readLine()) != null) {
                if (line.length() < 81) continue;

                Board board = Board.parseFrom(line);
                while (!unsafe.compareAndSetReference(base, puzzleOffset, null, board)) {
//                 while (!puzzle.compareAndSet(null, board)) {
                    Thread.yield(); // spin lock lol
                    if (!worker.isAlive()) {
                        throw new IllegalStateException("worker thread died");
                    }
                }
            }

            while (!unsafe.compareAndSetReference(base, puzzleOffset, null, END_EXECUTION)) {
//             while (!puzzle.compareAndSet(null, END_EXECUTION)) {
               Thread.yield(); // spin lock lol
               if (!worker.isAlive()) throw new IllegalStateException("worker thread died");
            }
        } finally {
            worker.join();
        }
	}
}