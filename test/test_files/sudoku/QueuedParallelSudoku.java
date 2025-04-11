import java.io.*;
import java.util.ArrayList;
import java.util.concurrent.LinkedBlockingQueue;

public class QueuedParallelSudoku {
    static final LinkedBlockingQueue<Board> puzzleQueue = new LinkedBlockingQueue<>();

	public static void main(String[] args) throws Exception {
		Solver solver = new Solver();

		ArrayList<Thread> workers = new ArrayList<>();

        int numWorkers = args.length == 0 ? 4 : Integer.parseInt(args[0]);
        System.out.println("Using " + numWorkers + " worker threads");
		for (int i=0; i<numWorkers; i++) {
		    Thread worker = new Thread(QueuedParallelSudoku::runWorker);
		    workers.add(worker);
            worker.start();
		}

		try (BufferedReader reader = new BufferedReader(new FileReader("test_files/sudoku/sudoku.txt"))) {
			String l;
			while ((l = reader.readLine()) != null) {
				if (l.length() >= 81) {
					Board board = Board.parseFrom(l);
					puzzleQueue.put(board);
				}
			}
		} catch (Exception e) {
		    for (Thread worker : workers) worker.interrupt();
            for (Thread worker : workers) worker.join();
            throw e;
		}

        // wait for all to be taken
		while (puzzleQueue.peek() != null) Thread.yield();

		for (Thread worker : workers) worker.interrupt();
		for (Thread worker : workers) worker.join();
	}

    public static void runWorker() {
        Solver solver = new Solver();
        try {
            while (!Thread.interrupted()) {
                Board board = puzzleQueue.take();

                Board result = solver.solve(board);
                if (!result.isSolved()) throw new IllegalArgumentException("Failed to solve " + board);
                System.out.println(result);
            }
        } catch (InterruptedException ignored) { }
    }
}