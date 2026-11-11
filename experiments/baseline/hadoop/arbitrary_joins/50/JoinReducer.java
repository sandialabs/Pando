import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Reducer;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class JoinReducer extends Reducer<Text, Text, Text, Text> {
    private final Text outKey = new Text();
    private final Text outValue = new Text();

    @Override
    public void reduce(Text key, Iterable<Text> values, Context context) throws IOException, InterruptedException {
        List<String> userIds = new ArrayList<>();
        String information = null;

        for (Text value : values) {
            String record = value.toString();
            if (record.startsWith("USER,")) {
                userIds.add(record.substring(5));
            } else if (record.startsWith("INFO,")) {
                information = record.substring(5);
            }
        }

        if (information != null) {
            String joinedValue = key.toString() + "\t" + information;
            for (String userId : userIds) {
                outKey.set(userId);
                outValue.set(joinedValue);
                context.write(outKey, outValue);
            }
        }
    }
}



// import org.apache.hadoop.io.LongWritable;
// import org.apache.hadoop.io.Text;
// import org.apache.hadoop.mapreduce.Reducer;
// import java.util.ArrayList;
// import java.util.List;
// import java.io.IOException;

// public class JoinReducer extends Reducer<Text, Text, Text, Text>
// {
//     @Override
//     public void reduce(Text key, Iterable<Text> values, Context context) throws IOException, InterruptedException 
//     {
//         List<String> userIds = new ArrayList<>();
//         String information = null;

//         // Separate user IDs and information
//         for (Text value : values) 
//         {
//             String record = value.toString();
//             if (record.startsWith("USER,")) 
//             {
//                 userIds.add(record.substring(5)); // Extract USER_ID
//             }
//             else if (record.startsWith("INFO,")) 
//             {
//                 information = record.substring(5); // Extract INFORMATION
//             }
//         }

//         // Emit joined records if information is available
//         if (information != null) 
//         {
//             for (String userId : userIds) 
//             {
//                 context.write(new Text(userId), new Text(key + "\t" + information)); // Emit USER_ID, PRODUCT_ID, INFORMATION
//             }
//         }
//     }
// }
