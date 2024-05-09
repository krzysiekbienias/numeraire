from scipy import stats as st



class ProbabilityInterface:

    def generate_random_numbers(self):
        pass

    def probability_density_function(self):
        pass

    def cumulative_distribution_function(self):
        pass


class NormalDistributionFamily(ProbabilityInterface):

    @staticmethod
    def generate_random_numbers(loc=0, scale=1, size=10):
        return st.norm.rvs(loc=loc, scale=scale, size=size)

    @staticmethod
    def probability_density_function(x, loc=0, scale=1):
        return st.norm.pdf(x, loc=loc, scale=scale)

    @staticmethod
    def cumulative_distribution_function(x, loc=0, scale=1):
        return st.norm.cdf(x, loc=loc, scale=scale)


class tStudentFamily(ProbabilityInterface):
    @staticmethod
    def probability_density_function(x,degree_of_freedom):
        return st.t.pdf(x, degree_of_freedom)

