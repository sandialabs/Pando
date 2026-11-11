import org.apache.hadoop.io.LongWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Mapper;

import java.io.IOException;

public class kmerMapper extends Mapper<LongWritable, Text, Text, LongWritable> {
    private int k = 15;

    private final Text outKey = new Text();
    private static final LongWritable ONE = new LongWritable(1);

    @Override
    protected void setup(Context context) throws IOException, InterruptedException {
        k = context.getConfiguration().getInt("kmer.length", 7);
    }

    @Override
    public void map(LongWritable key, Text value, Context context) throws IOException, InterruptedException {
        String line = value.toString().trim();

        int comma = line.indexOf(',');
        if (comma < 0) {
            return;
        }

        String seq = line.substring(comma + 1).trim();

        if (seq.length() < k) {
            return;
        }

        for (int i = 0; i + k <= seq.length(); i++) {
            String kmer = seq.substring(i, i + k);
            outKey.set(kmer);
            context.write(outKey, ONE);
        }
    }
}
