/* Scans through a 3D grid of parameters (Npp, k, x) and builds up  a refmult
 * distribution by sampling the npart vs ncoll distribution from the glauber
 * results N times for every set of parameters. Compares the generated refmult
 * distribution to a data refmult distribution using a chi2 similarity measure.
 * Reports the best fit parameters.
 *
 */

#include "sct/centrality/centrality.h"
#include "sct/centrality/nbd_fit.h"
#include "sct/lib/enumerations.h"
#include "sct/lib/flags.h"
#include "sct/lib/logging.h"
#include "sct/lib/string/string_cast.h"
#include "sct/lib/string/string_utils.h"
#include "sct/utils/random.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "boost/filesystem.hpp"

#include "TCanvas.h"
#include "TError.h"
#include "TFile.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TROOT.h"
#include "TTree.h"
#include "TTreeReader.h"
#include "TTreeReaderValue.h"

// program settings
SCT_DEFINE_string(outDir, "tmp",
                  "Path to directory to store output ROOT files");
SCT_DEFINE_string(outFile, "fit_results", "output file name (no extension)");
SCT_DEFINE_bool(saveAll, false,
                "save all fit histograms: if false, only saves best fit");
SCT_DEFINE_string(
    glauberFile, "npartncoll.root",
    "path to root file containing the npart x ncoll distribution");
SCT_DEFINE_string(glauberHistName, "npartncoll",
                  "name of npart x ncoll histogram");
SCT_DEFINE_string(dataFile, "refmult.root",
                  "path to root file containing data refmult distribution");
SCT_DEFINE_string(dataHistName, "refmult",
                  "name of reference multiplicty histogram");
SCT_DEFINE_int(events, 1e5, "number of events per fit");

// model settings
SCT_DEFINE_double(npp_min, 1.0, "minimum Npp for negative binomial");
SCT_DEFINE_double(npp_max, 4.0, "maximum Npp for negative binomial");
SCT_DEFINE_int(npp_steps, 31, "number of steps in Npp range to sample");
SCT_DEFINE_double(k_min, 1.0, "minimum k for negative binomial");
SCT_DEFINE_double(k_max, 4.0, "maximum k for negative binomial");
SCT_DEFINE_int(k_steps, 31, "number of steps in k range to sample");
SCT_DEFINE_double(x_min, 0.1, "minimum x for two component multiplicity");
SCT_DEFINE_double(x_max, 0.4, "maximum x for two component multiplicity");
SCT_DEFINE_int(x_steps, 31, "number of steps in x range to sample");
SCT_DEFINE_double(ppEfficiency, 0.98, "pp efficiency");
SCT_DEFINE_double(AuAuEfficiency, 0.84, "0-5% central AuAu efficiency");
SCT_DEFINE_int(centMult, 540, "average 0-5% central multiplicity");
SCT_DEFINE_bool(constEff, false, "turn on to use only pp efficiency");
SCT_DEFINE_bool(useStGlauberChi2, true,
                "use StGlauber Chi2 calculation instead of ROOT");
SCT_DEFINE_bool(useStGlauberNorm, true,
                "use StGlauber Normalization instead of integral norm");
SCT_DEFINE_double(trigBias, 1.0, "trigger bias");
SCT_DEFINE_int(minMult, 100,
               "minimum multiplicity for chi2 comparisons in fit");

int main(int argc, char *argv[]) {
  // shut ROOT up :)
  gErrorIgnoreLevel = kWarning;

  // set help message
  std::string usage =
      "Performs a grid search over the multiplicity model parameter space: ";
  usage += "[Npp, k, x, pp efficiency, central AuAu efficiency, trigger "
           "efficiency], using a chi2 ";
  usage += "fit to a measured refmult distribution as the objective function.";
  sct::SetUsageMessage(usage);

  sct::InitLogging(&argc, argv);
  sct::ParseCommandLineFlags(&argc, argv);

  // build output directory if it doesn't exist, using boost::filesystem
  boost::filesystem::path dir(FLAGS_outDir);
  boost::filesystem::create_directories(dir);

  // load the input files data refmult histogram & glauber npart x ncoll
  TFile *glauber_file = new TFile(FLAGS_glauberFile.c_str(), "READ");
  TFile *data_file = new TFile(FLAGS_dataFile.c_str(), "READ");

  if (!glauber_file->IsOpen()) {
    LOG(ERROR) << "Glauber input file could not be opened: "
               << FLAGS_glauberFile << " not found or corrupt";
    return 1;
  }
  if (!data_file->IsOpen()) {
    LOG(ERROR) << "Data input file could not be opened: " << FLAGS_dataFile
               << " not found or corrupt";
    return 1;
  }

  // load data refmult histogram & glauber npart x ncoll
  TH2D *npartncoll = (TH2D *)glauber_file->Get(FLAGS_glauberHistName.c_str());
  TH1D *refmult = (TH1D *)data_file->Get(FLAGS_dataHistName.c_str());

  if (npartncoll == nullptr) {
    LOG(ERROR) << "NPart x NColl histogram: " << FLAGS_glauberHistName
               << " not found in file: " << FLAGS_glauberFile;
    return 1;
  }
  if (refmult == nullptr) {
    LOG(ERROR) << "Reference multiplicity histogram: " << FLAGS_dataHistName
               << " not found in file: " << FLAGS_dataFile;
    return 1;
  }

  // create our fitting model
  sct::NBDFit fitter(refmult, npartncoll);
  fitter.minimumMultiplicityCut(FLAGS_minMult);
  fitter.useStGlauberChi2(FLAGS_useStGlauberChi2);
  fitter.useStGlauberNorm(FLAGS_useStGlauberNorm);

  // scan
  auto results = fitter.scan(
      FLAGS_events, FLAGS_npp_steps, FLAGS_npp_min, FLAGS_npp_max,
      FLAGS_k_steps, FLAGS_k_min, FLAGS_k_max, FLAGS_x_steps, FLAGS_x_min,
      FLAGS_x_max, FLAGS_ppEfficiency, FLAGS_AuAuEfficiency, FLAGS_centMult,
      FLAGS_trigBias, FLAGS_constEff, FLAGS_saveAll);

  // find best fit
  double best_chi2 = 9999;
  std::string best_key;

  // and we will parse the parameters
  double x = 0.0;
  double npp = 0.0;
  double k = 0.0;

  for (auto &result : results) {
    if (result.second->chi2 / result.second->ndf < best_chi2) {
      best_chi2 = result.second->chi2 / result.second->ndf;
      best_key = result.first;

      // parse the parameters
      std::vector<std::string> split;
      sct::SplitString(best_key, split, '_');
      npp = strtof(split[1].c_str(), 0);
      k = strtof(split[3].c_str(), 0);
      x = strtof(split[5].c_str(), 0);
    }
  }

  LOG(INFO) << "BEST FIT: " << best_key;
  LOG(INFO) << "chi2/ndf: " << best_chi2;

  // now we will generate a new simulation curve using the fitter,
  // but we will use greater statistics
  fitter.setParameters(npp, k, x, FLAGS_ppEfficiency, FLAGS_AuAuEfficiency,
                       FLAGS_centMult, FLAGS_trigBias, FLAGS_constEff);
  auto refit = fitter.fit(1e6);
  LOG(INFO) << "finished fitting";

  // and we save the results to disk
  std::string output_name = FLAGS_outDir + "/" + FLAGS_outFile + ".root";
  TFile out(output_name.c_str(), "RECREATE");
  refit->data->SetName("refmult");
  refit->data->Write();
  refit->simu->SetNameTitle("glauber", best_key.c_str());
  refit->simu->SetTitle(best_key.c_str());
  refit->simu->Write();

  // write all histograms to disk if they were saved
  if (FLAGS_saveAll) {
    for (auto &result : results) {
      double chi2 = result.second->chi2 / result.second->ndf;
      std::string name = result.first;
      std::string title =
          result.first + "_chi2/ndf=" +
          sct::MakeString(std::fixed, std::setprecision(5), chi2);
      result.second->simu->SetNameTitle(name.c_str(), title.c_str());
      result.second->simu->Write();
    }
  }

  out.Close();

  gflags::ShutDownCommandLineFlags();
  return 0;
}

// // if you want to create a reweighted refmult distribution, you can pass in a
// // tree with refmult info,
// SCT_DEFINE_string(refmultTreeFile, "", "file containing refmult tree");
// SCT_DEFINE_string(refmultTreeName, "refMultTree", "name of refmult tree");
// SCT_DEFINE_string(refmultBranchName, "refMult", "name of refmult branch");
// SCT_DEFINE_string(vzBranchName, "vz", "name of Vz branch");
// SCT_DEFINE_string(lumiBranchName, "lumi",
//                   "name of luminosity branch (zdc rate, bbc rate, etc)");
// SCT_DEFINE_string(preGlauberCorrFile, "",
//                   "name of file containing lumi & vz correction parameters");
// SCT_DEFINE_double(vzNorm, 0.0, "normalization point for vz corrections");
// SCT_DEFINE_double(lumiNorm, 0.0, "normalization point for zdcX corrections");

// double LumiScaling(double zdcX, double zdc_norm_point,
//                    std::vector<double> &pars) {
//   if (pars.size() != 2) {
//     LOG(ERROR) << "expected 2 parameters for luminosity scaling, returning 1";
//     return 1.0;
//   }
//   double zdc_scaling = pars[0] + pars[1] * zdcX / 1000.0;
//   double zdc_norm = pars[0] + pars[1] * zdc_norm_point / 1000.0;
//   return zdc_norm / zdc_scaling;
// }

// double VzScaling(double vz, double vz_norm_point, std::vector<double> &pars) {
//   if (pars.size() != 7) {
//     LOG(ERROR) << "expected 7 parameters for luminosity scaling, returning 1";
//     return 1.0;
//   }
//   double vz_scaling = 0.0;
//   double vz_norm = 0.0;
//   for (int i = 0; i < pars.size(); ++i) {
//     vz_scaling += pars[i] * pow(vz, i);
//     vz_norm += pars[i] * pow(vz_norm_point, i);
//   }
//   if ((vz_norm / vz_scaling) <= 0)
//     return 1.0;
//   return vz_norm / vz_scaling;
// }

// double GlauberScaling(double refmult, double cutoff, std::vector<double> pars) {
//   if (refmult > cutoff)
//     return 1;
//   double denom = pars[0] + pars[1] / (pars[2] * refmult + pars[3]) +
//                  pars[4] * (pars[2] * refmult + pars[3]);
//   denom += pars[5] / pow(pars[2] * refmult + pars[3], 2) +
//            pars[6] * pow(pars[2] * refmult + pars[3], 2);
//   return pars[0] / denom;
// }

// // now calculate centrality definition for the best fit
//   sct::Centrality cent;
//   cent.setDataRefmult(refit->data);
//   cent.setSimuRefmult(refit->simu.get());
//   std::vector<unsigned> cent_bounds = cent.centralityBins(sct::XSecMod::None);
//   std::vector<unsigned> cent_bounds_p5 =
//       cent.centralityBins(sct::XSecMod::Plus5);
//   std::vector<unsigned> cent_bounds_m5 =
//       cent.centralityBins(sct::XSecMod::Minus5);
//   LOG(INFO) << "finished calculating centrality";
//   // get weights
//   auto weights = cent.weights();

//   // write the ratio to file
//   weights.second->SetName("ratio_fit");
//   weights.second->Write();

//   // if we have a refmult tree file, we can create a reweighted refmult
//   // distribution and take the ratio with the glauber distribution
//   if (!FLAGS_refmultTreeFile.empty()) {
//     LOG(INFO) << "will be calculating corrected ratio";
//     // first, read in vz/luminosity corrections if we use them
//     std::vector<double> vz_pars;
//     std::vector<double> lumi_pars;
//     if (!FLAGS_preGlauberCorrFile.empty()) {
//       LOG(INFO) << "using luminosity & vz corrections";
//       std::ifstream glauber_in;
//       glauber_in.open(FLAGS_preGlauberCorrFile);
//       if (glauber_in.is_open()) {
//         std::string tmp_string;
//         while (std::getline(glauber_in, tmp_string)) {
//           auto result = sct::ParseStrToVec<double>(tmp_string);
//           if (result.size() == 2)
//             lumi_pars = result;
//           else if (result.size() == 7)
//             vz_pars = result;
//         }
//       }
//     }

  //   // setup TTreeReader
  //   LOG(INFO) << "reading in refmult tree";
  //   TFile refmult_tree_file(FLAGS_refmultTreeFile.c_str(), "read");
  //   TTreeReader reader(FLAGS_refmultTreeName.c_str(), &refmult_tree_file);
  //   TTreeReaderValue<unsigned> refmult(reader, FLAGS_refmultBranchName.c_str());
  //   TTreeReaderValue<double> vz(reader, FLAGS_vzBranchName.c_str());
  //   TTreeReaderValue<double> zdcx(reader, FLAGS_lumiBranchName.c_str());
  //   // create our histogram
  //   TH1D *refmult_scaled = new TH1D("weighted_refmult", "", 800, 0, 800);
  //   refmult_scaled->SetDirectory(0);

  //   while (reader.Next()) {
  //     double refmultcorr = *refmult;

  //     if (fabs(*vz) > 30 || fabs(*zdcx) > 100000)
  //       continue;

  //     if (lumi_pars.size() && vz_pars.size()) {
  //       double lumi_scaling = LumiScaling(*zdcx, FLAGS_lumiNorm, lumi_pars);
  //       double vz_scaling = VzScaling(*vz, FLAGS_vzNorm, vz_pars);
  //       refmultcorr *= lumi_scaling;
  //       refmultcorr *= vz_scaling;
  //     }

  //     double weight = GlauberScaling(refmultcorr, 400, weights.first);
  //     refmult_scaled->Fill(refmultcorr, 1.0 / weight);
  //   }

  //   TH1D *scaled_ratio = new TH1D(*refmult_scaled);
  //   scaled_ratio->SetDirectory(0);
  //   scaled_ratio->SetName("corrected_ratio");

  //   // normalize it
  //   double scale_factor = fitter.norm(scaled_ratio, refit->simu.get());
  //   scaled_ratio->Scale(1.0 / scale_factor);
  //   scaled_ratio->Divide(refit->simu.get());

  //   // write to file
  //   LOG(INFO) << "done with corrected spectra";
  //   refmult_tree_file.Close();
  //   out.cd();
  //   scaled_ratio->Write();
  //   refmult_scaled->Write();
  // }

  // std::ofstream cent_file;
  // cent_file.open(FLAGS_outDir + "/" + FLAGS_outFile + ".txt",
  //                std::ios::out | std::ios::app);

  // cent_file << "nominal cent: ";
  // for (auto i : cent_bounds)
  //   cent_file << i << " ";
  // cent_file << "\n";
  // cent_file << "+5% xsec cent: ";
  // for (auto i : cent_bounds_p5)
  //   cent_file << i << " ";
  // cent_file << "\n";
  // cent_file << "-5% xsec cent: ";
  // for (auto i : cent_bounds_m5)
  //   cent_file << i << " ";
  // cent_file << "\n";
  // cent_file << "weights: ";
  // for (auto par : weights.first)
  //   cent_file << par << " ";
  // cent_file << "\n";

  // cent_file.close();