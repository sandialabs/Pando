// import org.apache.hadoop.conf.Configuration;
// import org.apache.hadoop.fs.Path;
// import org.apache.hadoop.io.LongWritable;
// import org.apache.hadoop.io.Text;
// import org.apache.hadoop.mapreduce.Job;
// import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
// import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;
// import org.apache.hadoop.mapreduce.lib.output.NullOutputFormat;

// public class JoinDriver {
//     public static void main(String[] args) throws Exception 
//     {
//         if (args.length != 2) 
//         {
//             System.err.println("Usage: JoinDriver <input path> <output path>");
//             System.exit(-1);
//         }

//         Configuration conf = new Configuration();
//         conf.setInt("join_percentage", 10); 

//         Job job = Job.getInstance(conf, "Arbitrary Joins");
//         job.setJarByClass(JoinDriver.class);
//         job.setMapperClass(JoinMapper.class);
//         job.setReducerClass(JoinReducer.class);

//         job.setOutputKeyClass(Text.class);
//         job.setOutputValueClass(Text.class);
//         // job.setOutputFormatClass(NullOutputFormat.class);
//         
//         FileInputFormat.addInputPath(job, new Path(args[0]));
//         FileOutputFormat.setOutputPath(job, new Path(args[1]));
//         System.exit(job.waitForCompletion(true) ? 0 : 1);
//     }
// }
// 
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;

public class JoinDriver {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            System.err.println("Usage: JoinDriver <input path> <output path>");
            System.exit(-1);
        }

        Configuration conf = new Configuration();
        conf.setInt("join_percentage", 50);

        Job job = Job.getInstance(conf, "Arbitrary Joins");
        job.setJarByClass(JoinDriver.class);
        job.setMapperClass(JoinMapper.class);
        job.setReducerClass(JoinReducer.class);

        job.setMapOutputKeyClass(Text.class);
        job.setMapOutputValueClass(Text.class);
        job.setOutputKeyClass(Text.class);
        job.setOutputValueClass(Text.class);

        FileInputFormat.addInputPath(job, new Path(args[0]));
        FileOutputFormat.setOutputPath(job, new Path(args[1]));

        System.exit(job.waitForCompletion(true) ? 0 : 1);
    }
}
