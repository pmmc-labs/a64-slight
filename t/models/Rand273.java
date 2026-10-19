// The expected output of t/273-rand.slight, from Java's own
// SplittableRandom, which slight's random numbers are (D222): each draw
// as the test makes it, a fork as split(), pick as an index, and shuffle
// as a stable sort by a double drawn for each element.
//
//     cd t/models && javac Rand273.java && java Rand273 > ../273-rand.expected
import java.util.*;

public class Rand273 {
    static String list(Object... xs) {
        StringBuilder b = new StringBuilder("(");
        for (int i = 0; i < xs.length; i++) b.append(i > 0 ? " " : "").append(xs[i]);
        return b.append(")").toString();
    }

    static String dice(SplittableRandom r) {
        Object[] xs = new Object[12];
        for (int i = 0; i < 12; i++) xs[i] = r.nextLong(6);
        return list(xs);
    }

    static List<Integer> shuffle(SplittableRandom r, List<Integer> xs) {
        double[] keys = new double[xs.size()];
        Integer[] order = new Integer[xs.size()];
        for (int i = 0; i < xs.size(); i++) { keys[i] = r.nextDouble(); order[i] = i; }
        Arrays.sort(order, Comparator.comparingDouble(i -> keys[i]));   // stable
        List<Integer> out = new ArrayList<>();
        for (Integer i : order) out.add(xs.get(i));
        return out;
    }

    // A float as slight shows one, for the ones here: shortest, with a point.
    static String flt(double d) { return Double.toString(d); }

    public static void main(String[] args) {
        SplittableRandom root = new SplittableRandom(1);
        System.out.println(list(root.nextLong(10), root.nextLong(10), root.nextLong(1000000)));
        root = new SplittableRandom(42);
        System.out.println(dice(root));
        System.out.println(list(root.nextLong(1), root.nextLong(1024), root.nextLong(4611686018427387903L), flt(root.nextDouble()), flt(root.nextDouble())));
        root = new SplittableRandom(42);
        System.out.println(dice(root));
        root = new SplittableRandom(-7);
        System.out.println(list(root.nextLong(100), root.nextLong(3)));

        root = new SplittableRandom(7);
        SplittableRandom a = root.split(), b = root.split();
        long mine = root.nextLong(100);
        String as = list(a.nextLong(100), a.nextLong(100));
        SplittableRandom c = b.split();
        String bs = list(b.nextLong(100), c.nextLong(100));
        System.out.println(list(mine, as, bs));

        root = new SplittableRandom(9);
        String[] five = { "a", "b", "c", "d", "e" };
        System.out.println(list(five[(int) root.nextLong(5)], five[(int) root.nextLong(5)], "()"));
        List<Integer> ten = new ArrayList<>();
        for (int i = 0; i < 10; i++) ten.add(i);
        System.out.println(list(shuffle(root, ten).toArray()));
        shuffle(root, ten);                         // the one sorted again
        System.out.println(list("()", list(ten.toArray())));
    }
}
