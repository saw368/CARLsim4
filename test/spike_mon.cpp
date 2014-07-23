#include <snn.h>

#include "carlsim_tests.h"

// TODO: I should probably use a google tests figure for this to reduce the
// amount of redundant code, but I don't need to do that right now. -- KDC

/// ****************************************************************************
/// Function to read and return a 1D array with time and nid (in that order.
/// ****************************************************************************
void readAndReturnSpikeFile(const std::string fileName, int*& buffer, long &arraySize){
	FILE* pFile;
	long lSize;
	size_t result;
	pFile = fopen ( fileName.c_str() , "rb" );
	if (pFile==NULL) {fputs ("File error",stderr); exit (1);}
		
	// obtain file size:
	fseek (pFile , 0 , SEEK_END);
	lSize = ftell(pFile);
	arraySize = lSize/sizeof(uint);
	rewind (pFile);
	int* AERArray;
	AERArray = new int[lSize];
	memset(AERArray,0,sizeof(int)*lSize);
	// allocate memory to contain the whole file:
	buffer = (int*) malloc (sizeof(int)*lSize);
	if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}
		
	// copy the file into the buffer:
	result = fread (buffer,1,lSize,pFile);
	if (result != lSize) {fputs ("Reading error",stderr); exit (3);}
		
	// the whole file is now loaded in the memory buffer.
	for(int i=0;i<lSize/sizeof(int);i=i+2){
		AERArray[i]=buffer[i];
	}

	// terminate
	fclose (pFile);
}

/// ****************************************************************************
/// Function for reading and printing spike data written to a file
/// ****************************************************************************
void readAndPrintSpikeFile(const std::string fileName){
	FILE * pFile;
	long lSize;
	int* buffer;
	size_t result;
	pFile = fopen ( fileName.c_str() , "rb" );
	if (pFile==NULL) {fputs ("File error",stderr); exit (1);}
			
	// obtain file size:
	fseek (pFile , 0 , SEEK_END);
	lSize = ftell (pFile);
	rewind (pFile);
		
	// allocate memory to contain the whole file:
	buffer = (int*) malloc (sizeof(int)*lSize);
	if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}
		
	// copy the file into the buffer:
	result = fread (buffer,1,lSize,pFile);
	if (result != lSize) {fputs ("Reading error",stderr); exit (3);}
		
	// the whole file is now loaded in the memory buffer.
	for(int i=0;i<lSize/sizeof(int);i=i+2){
		printf("time = %d, nid = %d\n",buffer[i],buffer[i+1]);
	}

	// terminate
	fclose (pFile);
	free (buffer);
}

/*
 * This test verifies that the spike times written to file and AER struct match the ones from the simulation.
 * A PeriodicSpikeGenerator is used to periodically generate spikes, which allows us to know the exact spike times.
 * We run the simulation for a random number of milliseconds (most probably no full seconds), and read the spike file
 * afterwards. We expect all spike times to be multiples of the inter-spike interval, and the total number of spikes
 * to be exactly what it should be. The same must apply to the AER struct from the SpikeMonitor object.
 */
TEST(SPIKEMON, spikeTimes) {
	double rate = rand()%20 + 2.0;  // some random mean firing rate
	int isi = 1000/rate; // inter-spike interval

	CARLsim* sim;
	const int GRP_SIZE = rand()%5 + 1; // some random group size

	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=1; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);
		float COND_tAMPA=5.0, COND_tNMDA=150.0, COND_tGABAa=6.0, COND_tGABAb=150.0;
		int g0 = sim->createSpikeGeneratorGroup("Input",GRP_SIZE,EXCITATORY_NEURON);
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		sim->setConductances(true,COND_tAMPA,COND_tNMDA,COND_tGABAa,COND_tGABAb);
		sim->connect(g0,g1,"random", RangeWeight(0.27f), 1.0f, RangeDelay(1), SYN_FIXED);

		// use periodic spike generator to know the exact spike times
		PeriodicSpikeGenerator* spkGen = new PeriodicSpikeGenerator(rate);
		sim->setSpikeGenerator(g0, spkGen);

		sim->setupNetwork();

		// write all spikes to file
		SpikeMonitor* spikeMonG0 = sim->setSpikeMonitor(g0,"spkG0.dat",0);
		spikeMonG0->startRecording();

		// pick some random simulation time
		int runMs = (5+rand()%20) * isi;
		sim->runNetwork(runMs/1000,runMs%1000);

		spikeMonG0->stopRecording();

		// get spike vector
		std::vector<AER> spkVector = spikeMonG0->getVector();

		// read spike file
		int* inputArray;
		long inputSize;
		readAndReturnSpikeFile("spkG0.dat",inputArray,inputSize);
//		readAndPrintSpikeFile("spkG0.dat");

		// sanity-check the size of the arrays
		EXPECT_EQ(inputSize/2, runMs/isi * GRP_SIZE);
		EXPECT_EQ(spkVector.size(), runMs/isi * GRP_SIZE);

		// check the spike times of spike file and AER struct
		// we expect all spike times to be a multiple of the ISI
		for (int i=0; i<inputSize; i+=2) {
			EXPECT_EQ(inputArray[i]%isi, 0);
			EXPECT_EQ(spkVector[i/2].time % isi, 0);
		}

		system("rm -rf spkG0.dat");
		delete[] inputArray;
		delete spkGen;
		delete sim;
	}
}

/*
 * This test checks for the correctness of the getGrpFiringRate method.
 * A PeriodicSpikeGenerator is used to periodically generate input spikes, so that the input spike times are known.
 * A network will then be run for a random amount of milliseconds. The activity of the input group will only be
 * recorded for a brief amount of time, whereas the activity of another group will be recorded for the full run.
 * The firing rate of the input group, which is calculated by the SpikeMonitor object, must then be based on only a
 * brief time window, whereas the spike file should contain all spikes. For the other group, both spike file and AER
 * struct should have the same number of spikes.
 */
TEST(SPIKEMON, getGrpFiringRate){
	CARLsim* sim;

	double rate = rand()%20 + 2.0;  // some random mean firing rate
	int isi = 1000/rate; // inter-spike interval

	const int GRP_SIZE = rand()%5 + 1;
	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=0; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);

		float COND_tAMPA=5.0, COND_tNMDA=150.0, COND_tGABAa=6.0, COND_tGABAb=150.0;
		int g0 = sim->createSpikeGeneratorGroup("Input",GRP_SIZE,EXCITATORY_NEURON);
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		sim->setConductances(true,COND_tAMPA,COND_tNMDA,COND_tGABAa,COND_tGABAb);
		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		sim->connect(g0,g1,"random", RangeWeight(0.27f), 1.0f, RangeDelay(1), SYN_FIXED);

		PeriodicSpikeGenerator* spkGen = new PeriodicSpikeGenerator(rate);
		sim->setSpikeGenerator(g0, spkGen);

		sim->setupNetwork();

		SpikeMonitor* spikeMonInput = sim->setSpikeMonitor(g0,"spkInputGrp.dat",0);
		SpikeMonitor* spikeMonG1 = sim->setSpikeMonitor(g1,"spkG1Grp.dat",0);

		// pick some random simulation time
		int runTimeMsOff = (5+rand()%10) * isi;
		int runTimeMsOn  = (5+rand()%20) * isi;

		// run network with recording off for g0, but recording on for G1
		spikeMonG1->startRecording();
		sim->runNetwork(runTimeMsOff/1000, runTimeMsOff%1000);

		// then start recording for some period
		spikeMonInput->startRecording();
		sim->runNetwork(runTimeMsOn/1000, runTimeMsOn%1000);
		spikeMonInput->stopRecording();

		// and run some more with recording off for input
		sim->runNetwork(runTimeMsOff/1000, runTimeMsOff%1000);

		// stopping the recording will update both AER structs and spike files
		spikeMonG1->stopRecording();

		// read spike files (which are now complete because of stopRecording above)
		int* inputArray;
		long inputSize;
		readAndReturnSpikeFile("spkInputGrp.dat",inputArray,inputSize);
		int* g1Array;
		long g1Size;
		readAndReturnSpikeFile("spkG1Grp.dat",g1Array,g1Size);

		// activity in the input group was recorded only for a short period
		// the SpikeMon object must thus compute the firing rate based on only a brief time window
		EXPECT_FLOAT_EQ(spikeMonInput->getGrpFiringRate(), rate); // rate must match
		EXPECT_EQ(spikeMonInput->getSize(), runTimeMsOn*GRP_SIZE/isi); // spikes only from brief window
		EXPECT_EQ(inputSize/2, (runTimeMsOn+2*runTimeMsOff)*GRP_SIZE/isi); // but spike file must have all spikes

		// g1 had recording on the whole time
		// its firing rate is not known explicitly, but AER should match spike file
		EXPECT_EQ(spikeMonG1->getSize(), g1Size/2);
		EXPECT_FLOAT_EQ(spikeMonG1->getGrpFiringRate(), g1Size/(2.0*GRP_SIZE) * 1000.0/(runTimeMsOn+2*runTimeMsOff));

		system("rm -rf spkInputGrp.dat");
		system("rm -rf spkG1Grp.dat");

		delete[] inputArray;
		delete[] g1Array;
		delete spkGen;
		delete sim;
	}
}



/// ****************************************************************************
/// TESTS FOR SET SPIKE MON 
/// ****************************************************************************

/*!
 * \brief testing to make sure grpId error is caught in setSpikeMonitor.
 *
 */
/*TEST(SETSPIKEMON, grpId){
	CARLsim* sim;
	const int GRP_SIZE = 10;
	
	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=1; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);
		
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		int g2 = sim->createGroup("g2", GRP_SIZE, EXCITATORY_NEURON, ALL);
		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		sim->setNeuronParameters(g2, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		
		EXPECT_DEATH(sim->setSpikeMonitor(ALL),"");  // grpId = ALL (-1) and less than 0 
		EXPECT_DEATH(sim->setSpikeMonitor(-4),"");  // less than 0
		EXPECT_DEATH(sim->setSpikeMonitor(2),""); // greater than number of groups
		EXPECT_DEATH(sim->setSpikeMonitor(MAX_GRP_PER_SNN),""); // greater than number of group & and greater than max groups
		
		delete sim;
	}
}*/

/*!
 * \brief testing to make sure configId error is caught in setSpikeMonitor.
 *
 */
/*TEST(SETSPIKEMON, configId){
	CARLsim* sim;
	const int GRP_SIZE = 10;
	
	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=1; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);
		
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		int g2 = sim->createGroup("g2", GRP_SIZE, EXCITATORY_NEURON, ALL);
		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		sim->setNeuronParameters(g2, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		
		EXPECT_DEATH(sim->setSpikeMonitor(1,"testSpikes.dat",ALL),"");  // configId = ALL (-1) and less than 0 
		EXPECT_DEATH(sim->setSpikeMonitor(1,"testSpikes.dat",-2),"");  // less than 0
		EXPECT_DEATH(sim->setSpikeMonitor(1,"testSpikes.dat",-100),"");  // less than 0

		delete sim;
	}
}*/


/*!
 * \brief testing to make sure file name error is caught in setSpikeMonitor.
 *
 */
/*TEST(SETSPIKEMON, fname){
	CARLsim* sim;
	const int GRP_SIZE = 10;
	
	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=1; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);
		
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		int g2 = sim->createGroup("g2", GRP_SIZE, EXCITATORY_NEURON, ALL);
		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		sim->setNeuronParameters(g2, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);

		// this directory doesn't exist.
		EXPECT_DEATH(sim->setSpikeMonitor(1,"absentDirectory/testSpikes.dat",0),"");  
		
		delete sim;
	}
}*/

/*!
 * \brief testing to make sure clear() function works.
 *
 */
/*TEST(SPIKEMON, clear){
	CARLsim* sim;
	PoissonRate* input;
	const int GRP_SIZE = 5;
	const int inputTargetFR = 5.0f;
	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=1; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);

		float COND_tAMPA=5.0, COND_tNMDA=150.0, COND_tGABAa=6.0, COND_tGABAb=150.0;
		int inputGroup = sim->createSpikeGeneratorGroup("Input",GRP_SIZE,EXCITATORY_NEURON);
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		int g2 = sim->createGroup("g2", GRP_SIZE, EXCITATORY_NEURON, ALL);
		
		sim->setConductances(true,COND_tAMPA,COND_tNMDA,COND_tGABAa,COND_tGABAb);
		double initWeight = 0.05f;
		

		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		sim->setNeuronParameters(g2, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);

		// input
		input = new PoissonRate(GRP_SIZE);
		for(int i=0;i<GRP_SIZE;i++){
			input->rates[i]=inputTargetFR;
		}
		sim->connect(inputGroup,g1,"random", RangeWeight(initWeight), 0.5f, RangeDelay(1), SYN_FIXED);
		sim->connect(inputGroup,g2,"random", RangeWeight(initWeight), 0.5f, RangeDelay(1), SYN_FIXED);
		sim->connect(g1,g2,"random", RangeWeight(initWeight), 0.5f, RangeDelay(1), SYN_FIXED);

		SpikeMonitor* spikeMonG1 = sim->setSpikeMonitor(g1);
		
		sim->setupNetwork();

		sim->setSpikeRate(inputGroup,input);
		
		spikeMonG1->startRecording();
		
		int runTimeMs = 2000;
		// run the network
		sim->runNetwork(runTimeMs/1000,runTimeMs%1000);
	
		spikeMonG1->stopRecording();
		
		// we should have spikes!
		EXPECT_TRUE(spikeMonG1->getSize() != 0);
		
		// now clear the spikes
		spikeMonG1->clear();

		// we shouldn't have spikes!
		EXPECT_TRUE(spikeMonG1->getSize() == 0);
		
		// start recording again
		spikeMonG1->startRecording();
		
		// run the network again
		sim->runNetwork(runTimeMs/1000,runTimeMs%1000);
		
		// stop recording
		spikeMonG1->stopRecording();
		
		// we should have spikes again
		EXPECT_TRUE(spikeMonG1->getSize() != 0);

		
		delete sim;
		delete input;
	}
}

TEST(SPIKEMON, getGrpFiringRateDeath) {
	CARLsim* sim;
	PoissonRate* input;
	const int GRP_SIZE = 5;
	const int inputTargetFR = 5.0f;

	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=1; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);

		float COND_tAMPA=5.0, COND_tNMDA=150.0, COND_tGABAa=6.0, COND_tGABAb=150.0;
		int inputGroup = sim->createSpikeGeneratorGroup("Input",GRP_SIZE,EXCITATORY_NEURON);
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		
		sim->setConductances(true,COND_tAMPA,COND_tNMDA,COND_tGABAa,COND_tGABAb);
		double initWeight = 0.27f;
		

		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);
		sim->connect(inputGroup,g1,"random", RangeWeight(initWeight), 1.0f, RangeDelay(1), SYN_FIXED);
		sim->setupNetwork();

		// input
		input = new PoissonRate(GRP_SIZE);
		for(int i=0;i<GRP_SIZE;i++){
			input->rates[i]=inputTargetFR;
		}

		sim->setSpikeRate(inputGroup,input);

		system("rm -rf spkInputGrp.dat");
		system("rm -rf spkG1Grp.dat");
		SpikeMonitor* spikeMonInput = sim->setSpikeMonitor(inputGroup,"spkInputGrp.dat",0);
		SpikeMonitor* spikeMonG1 = sim->setSpikeMonitor(g1,"spkG1Grp.dat",0);
		
		spikeMonInput->startRecording();
		spikeMonG1->startRecording();
	 		
		int runTimeMs = 2000;
		// run the network
		sim->runNetwork(runTimeMs/1000,runTimeMs%1000);
	
		// don't call stopRecording here
//		spikeMonInput->stopRecording();
//		spikeMonG1->stopRecording();

		// confirm the spike info information is correct here.
		EXPECT_DEATH(spikeMonInput->getGrpFiringRate(),"");
		EXPECT_DEATH(spikeMonG1->getGrpFiringRate(),"");
		
		delete sim;
		delete input;
	}
}

TEST(SPIKEMON, getMaxMinNeuronFiringRate){
	CARLsim* sim;
	PoissonRate* input;
	const int GRP_SIZE = 5;
	const int inputTargetFR = 5.0f;
	// use threadsafe version because we have deathtests
	::testing::FLAGS_gtest_death_test_style = "threadsafe";

	// loop over both CPU and GPU mode.
	for(int mode=0; mode<=1; mode++){
		// first iteration, test CPU mode, second test GPU mode
		sim = new CARLsim("SNN",mode?GPU_MODE:CPU_MODE,SILENT,0,1,42);

		float COND_tAMPA=5.0, COND_tNMDA=150.0, COND_tGABAa=6.0, COND_tGABAb=150.0;
		int inputGroup = sim->createSpikeGeneratorGroup("Input",GRP_SIZE,EXCITATORY_NEURON);
		int g1 = sim->createGroup("g1", GRP_SIZE, EXCITATORY_NEURON, ALL);
		
		sim->setConductances(true,COND_tAMPA,COND_tNMDA,COND_tGABAa,COND_tGABAb);

		sim->setNeuronParameters(g1, 0.02f, 0.0f, 0.2f, 0.0f, -65.0f, 0.0f, 8.0f, 0.0f, ALL);

		sim->connect(inputGroup,g1,"random", RangeWeight(0.27f), 1.0f, RangeDelay(1), SYN_FIXED);

		sim->setupNetwork();

		// input
		input = new PoissonRate(GRP_SIZE);
		for(int i=0;i<GRP_SIZE;i++){
			input->rates[i]=inputTargetFR;
		}

		sim->setSpikeRate(inputGroup,input);

		system("rm -rf spkInputGrp.dat");
		system("rm -rf spkG1Grp.dat");
		SpikeMonitor* spikeMonInput = sim->setSpikeMonitor(inputGroup,"spkInputGrp.dat",0);
		SpikeMonitor* spikeMonG1 = sim->setSpikeMonitor(g1,"spkG1Grp.dat",0);
		
		spikeMonInput->startRecording();
		spikeMonG1->startRecording();
	 		
		int runTimeMs = 2000;
		// run the network
		sim->runNetwork(runTimeMs/1000,runTimeMs%1000);
	
		spikeMonInput->stopRecording();
		spikeMonG1->stopRecording();

		int* inputArray;
		long inputSize;
		readAndReturnSpikeFile("spkInputGrp.dat",inputArray,inputSize);
		int* g1Array;
		long g1Size;
		readAndReturnSpikeFile("spkG1Grp.dat",g1Array,g1Size);
		sim->setSpikeRate(inputGroup,input);
		// divide both by two, because we are only counting spike events, for 
		// which there are two data elements (time, nid)
		int inputSpkCount[GRP_SIZE];
		int g1SpkCount[GRP_SIZE];
		memset(inputSpkCount,0,sizeof(int)*GRP_SIZE);
		memset(g1SpkCount,0,sizeof(int)*GRP_SIZE);
		for(int i=0;i<inputSize;i=i+2){
			inputSpkCount[inputArray[i+1]]++;
		}
		for(int i=0;i<g1Size;i=i+2){
			g1SpkCount[g1Array[i+1]]++;
		}

		std::vector<float> inputVector;
		std::vector<float> g1Vector;
		for(int i=0;i<GRP_SIZE;i++){
			//float inputFR = ;
			//float g1FR = ;
			inputVector.push_back(inputSpkCount[i]*1000.0/(float)runTimeMs);
			g1Vector.push_back(g1SpkCount[i]*1000.0/(float)runTimeMs);
		}
		// confirm the spike info information is correct here.
		std::sort(inputVector.begin(),inputVector.end());
		std::sort(g1Vector.begin(),g1Vector.end());

		// check max neuron firing
		EXPECT_FLOAT_EQ(spikeMonInput->getMaxNeuronFiringRate(),inputVector.back());
		EXPECT_FLOAT_EQ(spikeMonG1->getMaxNeuronFiringRate(),g1Vector.back());

		// check min neuron firing
		EXPECT_FLOAT_EQ(spikeMonInput->getMinNeuronFiringRate(),inputVector.front());
		EXPECT_FLOAT_EQ(spikeMonG1->getMinNeuronFiringRate(),g1Vector.front());
		
		delete inputArray;
		delete g1Array;
		delete sim;
		delete input;
	}
}*/