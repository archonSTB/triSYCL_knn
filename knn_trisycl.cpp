#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <CL/sycl.hpp>

using namespace cl::sycl;

constexpr size_t training_set_size = 5000;
constexpr size_t data_size = 784;

using Vector = std::array<int, data_size>;

struct Img {
  int label;
  Vector pixels;
};

std::vector<Img> training_set;
std::vector<Img> validation_set;

buffer<int> get_buffer(const std::vector<Img>& imgs) {
  std::vector<int> res;
  for (Img elem : imgs) {
    res.insert(res.end(), std::begin(elem.pixels), std::end(elem.pixels));
  }
  return buffer<int> { std::begin(res), std::end(res) };
}

std::vector<Img> slurp_file(const std::string& name) {
  std::ifstream infile { name, std::ifstream::in };
  std::cout << "Loading " << name << std::endl;
  std::string line, token;
  std::vector<Img> res;
  bool fst_1 = true;
  while (std::getline(infile, line)) {
    if (fst_1) {
      fst_1 = false;
      continue;
    }
    Img img;
    std::istringstream iss { line };
    bool fst = true;
    int index = 0;
    while (std::getline(iss, token, ',')) {
      if (fst) {
        img.label = std::stoi(token);
        fst = false;
      }
      else {
        img.pixels[index] = std::stoi(token);
        index++;
      }
    }
    res.push_back(img);
  }
  std::cout << "Done" << std::endl;
  return res;
}

int search_image(buffer<int>& training, const Img& img, queue& q) {
  int res[training_set_size];

  {
    buffer<int> A { std::begin(img.pixels), std::end(img.pixels) };
    buffer<int> B { res, training_set_size };
    // Compute the L2 distance between of an image against the training set
    q.submit([&] (handler &cgh) {
        auto train = training.get_access<access::mode::read>(cgh);
        auto ka = A.get_access<access::mode::read>(cgh);
        auto kb = B.get_access<access::mode::write>(cgh);
        cgh.parallel_for(range<1> { training_set_size }, [=] (id<1> index) {
            decltype(ka)::value_type diff = 0;
            for (auto i = 0; i != data_size; i++) {
              auto toAdd = ka[i] - train[index[0]*data_size + i];
              diff += toAdd*toAdd;
            }
            kb[index] = diff;
          });
      });
  }

  // Find the image with the minimum distance
  auto min_image = std::min_element(std::begin(res), std::end(res));

  // Test if we found the good digit
  return
    training_set[std::distance(std::begin(res), min_image)].label == img.label;
}

int main(int argc, char* argv[]) {
  int correct = 0;
  training_set = slurp_file("data/trainingsample.csv");
  validation_set =  slurp_file("data/validationsample.csv");
  buffer<int> training_buffer = get_buffer(training_set);

  queue q;

  auto start_time = std::chrono::high_resolution_clock::now();

  for (auto const & img : validation_set)
    correct += search_image(training_buffer, img, q);

  std::chrono::duration<double, std::milli> duration_ms =
    std::chrono::high_resolution_clock::now() - start_time;

  std::cout << (duration_ms.count()/validation_set.size())
            << "ms/kernel" << std::endl;

  std::cout << "\nResult : " << (100.0*correct/validation_set.size()) << '%'
            << " (" << correct << ")"
            << std::endl;
  return 0;
}
