import org.apache.hadoop.io.LongWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Mapper;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class IndexMapper extends Mapper<LongWritable, Text, Text, Text> {
    private int k = 15;

    private final Text outKey = new Text();
    private final Text outValue = new Text();

    @Override
    protected void setup(Context context) throws IOException, InterruptedException {
        k = context.getConfiguration().getInt("kmer.length", 15);
    }

    @Override
    public void map(LongWritable key, Text value, Context context) throws IOException, InterruptedException {
        String line = value.toString().trim();
        int comma = line.indexOf(',');

        if (comma < 0) {
            return;
        }

        String readId = line.substring(0, comma);
        String seq = line.substring(comma + 1).trim();

        if (seq.length() < k) {
            return;
        }

        Map<String, List<Integer>> kmerPositions = new HashMap<>();

        for (int i = 0; i + k <= seq.length(); i++) {
            String kmer = seq.substring(i, i + k);
            kmerPositions.computeIfAbsent(kmer, x -> new ArrayList<>()).add(i);
        }

        StringBuilder sb = new StringBuilder();

        for (Map.Entry<String, List<Integer>> entry : kmerPositions.entrySet()) {
            sb.setLength(0);
            sb.append(readId).append('\t');

            List<Integer> positions = entry.getValue();
            for (int i = 0; i < positions.size(); i++) {
                if (i > 0) {
                    sb.append(',');
                }
                sb.append(positions.get(i));
            }

            outKey.set(entry.getKey());
            outValue.set(sb.toString());
            context.write(outKey, outValue);
        }
    }
}
